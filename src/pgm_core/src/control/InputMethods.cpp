#include "Methods.hpp"

#include <spdlog/fmt/fmt.h>

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace pgm::control
{

namespace
{

/// Player 1's controls, in the simulator's encoding of `input.get_state`: its
/// joystick_p1 bits, then start from bit 16. Button 4 and the coin are the
/// emulator's own; the simulator has no name for them.
struct Control
{
  std::string_view name;
  std::uint32_t bit;
};

constexpr std::uint32_t START = 1U << 16U;
constexpr std::uint32_t COIN = 1U << 20U;

constexpr std::array<Control, 11> CONTROLS{ { { .name = "right", .bit = 0x01 },
                                              { .name = "left", .bit = 0x02 },
                                              { .name = "down", .bit = 0x04 },
                                              { .name = "up", .bit = 0x08 },
                                              { .name = "button1", .bit = 0x10 },
                                              { .name = "btn1", .bit = 0x10 },
                                              { .name = "a", .bit = 0x10 },
                                              { .name = "button2", .bit = 0x20 },
                                              { .name = "button3", .bit = 0x40 },
                                              { .name = "button4", .bit = 0x80 },
                                              { .name = "start", .bit = START } } };

std::expected<std::uint32_t, Error> bitOf( Json const& params )
{
  auto const name = requireString( params, "name" );
  if ( !name )
  {
    return std::unexpected( name.error() );
  }
  if ( *name == "coin" )
  {
    return COIN;
  }
  for ( Control const& control : CONTROLS )
  {
    if ( control.name == *name )
    {
      return control.bit;
    }
  }
  return std::unexpected( Error{ .code = "invalid_input", .message = fmt::format( "Unknown input: {}", *name ) } );
}

/// The buttons as PGM.sv's IN0..IN3 words hold them: IN0's low byte is start,
/// up, down, left, right and buttons 1-3; IN2 has the coin in bit 0 and
/// button 4 in bit 8.
std::array<std::uint16_t, 4> inputWords( std::uint32_t buttons )
{
  auto const has = [buttons]( std::uint32_t bit ) { return ( buttons & bit ) != 0; };
  std::uint32_t in0 = 0;
  in0 |= has( START ) ? 0x01U : 0U;
  in0 |= has( 0x08 ) ? 0x02U : 0U;
  in0 |= has( 0x04 ) ? 0x04U : 0U;
  in0 |= has( 0x02 ) ? 0x08U : 0U;
  in0 |= has( 0x01 ) ? 0x10U : 0U;
  in0 |= ( buttons & 0x70U ) << 1U;
  std::uint32_t const in2 = ( has( COIN ) ? 0x01U : 0U ) | ( has( 0x80 ) ? 0x100U : 0U );
  return { static_cast<std::uint16_t>( in0 ), 0, static_cast<std::uint16_t>( in2 ), 0 };
}

Error notLoaded()
{
  return Error{ .code = "not_loaded", .message = "No game is loaded" };
}

} // namespace

void addInputMethods( Dispatcher& dispatcher, Emulator& emulator )
{
  // The buttons held through the protocol, shared by the methods below.
  auto buttons = std::make_shared<std::uint32_t>( 0 );

  // The machine a request acts on and the control it names, or why not.
  auto const target =
      [&emulator]( Json const& params ) -> std::expected<std::pair<machine::Machine*, std::uint32_t>, Error>
  {
    machine::Machine* const machine = emulator.machine();
    auto const bit = bitOf( params );
    if ( machine == nullptr || !bit )
    {
      return std::unexpected( machine == nullptr ? notLoaded() : bit.error() );
    }
    return std::pair{ machine, *bit };
  };
  auto const hold = [buttons]( machine::Machine& machine, std::uint32_t bit, bool pressed )
  {
    *buttons = pressed ? *buttons | bit : *buttons & ~bit;
    machine.setInputs( inputWords( *buttons ) );
  };

  for ( bool const pressed : { true, false } )
  {
    dispatcher.add( pressed ? "input.set" : "input.clear",
                    pressed
                        ? info( "Holds a control of player 1 down until input.clear.",
                                { { .name = "name",
                                    .type = "string",
                                    .description = "up, down, left, right, button1 to button4, start or coin." } } )
                        : info( "Lets a control of player 1 go.",
                                { { .name = "name",
                                    .type = "string",
                                    .description = "up, down, left, right, button1 to button4, start or coin." } } ),
                    [target, hold, pressed]( Json const& params ) -> Outcome
                    {
                      auto const found = target( params );
                      if ( !found )
                      {
                        return std::unexpected( found.error() );
                      }
                      hold( *found->first, found->second, pressed );
                      return Json::object();
                    } );
  }

  dispatcher.add( "input.press",
                  info( "Presses a control of player 1 for two frames and releases it for two: four frames run.",
                        { { .name = "name",
                            .type = "string",
                            .description = "up, down, left, right, button1 to button4, start or coin." } } ),
                  [target, hold]( Json const& params ) -> Outcome
                  {
                    auto const found = target( params );
                    if ( !found )
                    {
                      return std::unexpected( found.error() );
                    }
                    // As the simulator presses: held for two frames, then
                    // released for two.
                    auto const [machine, bit] = *found;
                    hold( *machine, bit, true );
                    machine::RunResult const down = machine->runFrames( 2 );
                    hold( *machine, bit, false );
                    machine::RunResult const up = machine->runFrames( 2 );
                    return Json{ { "reason", "completed" },
                                 { "ticks_executed", down.ticks + up.ticks },
                                 { "frames_executed", down.frames + up.frames } };
                  } );

  dispatcher.add( "input.get_state",
                  info( "The controls held, in the RTL simulator's encoding." ),
                  [buttons]( Json const& /*params*/ ) -> Outcome { return Json{ { "buttons", *buttons } }; } );
}

} // namespace pgm::control
