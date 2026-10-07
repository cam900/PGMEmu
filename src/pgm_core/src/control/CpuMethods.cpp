#include "Methods.hpp"

#include <spdlog/fmt/fmt.h>

#include <string>

namespace pgm::control
{

namespace
{

/// The most instructions one `cpu.disassemble` answers.
constexpr std::uint64_t MAX_INSTRUCTIONS = 1000;

std::expected<machine::Machine*, Error> loadedMachine( Emulator& emulator )
{
  machine::Machine* const machine = emulator.machine();
  if ( machine == nullptr )
  {
    return std::unexpected( Error{ .code = "not_loaded", .message = "No game is loaded" } );
  }
  return machine;
}

Json stateOf( machine::Machine const& machine )
{
  machine::M68kState const state = machine.m68kState();
  // `registers` is the simulator's: the 17 longs of fx68k's register file,
  // D0-D7, A0-A6, then the user and supervisor stack pointers.
  Json registers = Json::array();
  for ( std::uint32_t const value : state.d )
  {
    registers.push_back( value );
  }
  for ( std::size_t i = 0; i < 7; ++i )
  {
    registers.push_back( state.a.at( i ) );
  }
  registers.push_back( state.usp );
  registers.push_back( state.ssp );

  int length = 0;
  return Json{ { "pc", state.pc },
               { "registers", std::move( registers ) },
               { "disasm", machine.disassemble( state.pc, length ) },
               { "d", state.d },
               { "a", state.a },
               { "sr", state.sr },
               { "usp", state.usp },
               { "ssp", state.ssp },
               { "stopped", state.stopped },
               { "halted", state.halted } };
}

} // namespace

void addCpuMethods( Dispatcher& dispatcher, Emulator& emulator )
{
  dispatcher.add( "cpu.get_state",
                  [&emulator]( Json const& params ) -> Outcome
                  {
                    auto const machine = loadedMachine( emulator );
                    if ( !machine )
                    {
                      return std::unexpected( machine.error() );
                    }
                    if ( params.contains( "cpu" ) && params.at( "cpu" ) != "m68k" )
                    {
                      return std::unexpected( badRequest( "Only the m68k is emulated yet" ) );
                    }
                    return stateOf( **machine );
                  } );

  dispatcher.add( "cpu.disassemble",
                  [&emulator]( Json const& params ) -> Outcome
                  {
                    auto const machine = loadedMachine( emulator );
                    auto const address = requireUnsigned( params, "address" );
                    auto const count = requireUnsigned( params, "count" );
                    if ( !machine )
                    {
                      return std::unexpected( machine.error() );
                    }
                    if ( !address )
                    {
                      return std::unexpected( address.error() );
                    }
                    if ( !count )
                    {
                      return std::unexpected( count.error() );
                    }
                    if ( *count > MAX_INSTRUCTIONS )
                    {
                      return std::unexpected(
                          badRequest( fmt::format( "count is above the limit of {}", MAX_INSTRUCTIONS ) ) );
                    }
                    Json lines = Json::array();
                    auto at = static_cast<std::uint32_t>( *address );
                    for ( std::uint64_t i = 0; i < *count; ++i )
                    {
                      int length = 0;
                      std::string text = ( *machine )->disassemble( at, length );
                      lines.push_back( Json{ { "address", at }, { "length", length }, { "text", std::move( text ) } } );
                      at += static_cast<std::uint32_t>( length > 0 ? length : 2 );
                    }
                    return lines;
                  } );

  dispatcher.add( "debug.breakpoint.add",
                  [&emulator]( Json const& params ) -> Outcome
                  {
                    auto const machine = loadedMachine( emulator );
                    auto const address = requireUnsigned( params, "address" );
                    if ( !machine || !address )
                    {
                      return std::unexpected( !machine ? machine.error() : address.error() );
                    }
                    ( *machine )->addBreakpoint( static_cast<std::uint32_t>( *address ) );
                    return Json::object();
                  } );

  dispatcher.add( "debug.breakpoint.remove",
                  [&emulator]( Json const& params ) -> Outcome
                  {
                    auto const machine = loadedMachine( emulator );
                    auto const address = requireUnsigned( params, "address" );
                    if ( !machine || !address )
                    {
                      return std::unexpected( !machine ? machine.error() : address.error() );
                    }
                    ( *machine )->removeBreakpoint( static_cast<std::uint32_t>( *address ) );
                    return Json::object();
                  } );

  dispatcher.add( "debug.breakpoint.list",
                  [&emulator]( Json const& /*params*/ ) -> Outcome
                  {
                    auto const machine = loadedMachine( emulator );
                    if ( !machine )
                    {
                      return std::unexpected( machine.error() );
                    }
                    Json addresses = Json::array();
                    for ( std::uint32_t const address : ( *machine )->breakpoints() )
                    {
                      addresses.push_back( address );
                    }
                    return addresses;
                  } );
}

} // namespace pgm::control
