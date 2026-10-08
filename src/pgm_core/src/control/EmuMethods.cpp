#include "Methods.hpp"

#include "pgm/Version.hpp"
#include "pgm/control/Describe.hpp"

#include <utility>

namespace pgm::control
{

namespace
{

Error loadError( LoadFailure const& failure )
{
  return Error{ .code = failure.kind == LoadFailure::Kind::UNKNOWN_GAME ? "unknown_game" : "load_failed",
                .message = failure.message };
}

} // namespace

void addEmuMethods( Dispatcher& dispatcher, Emulator& emulator )
{
  dispatcher.add( "emu.status",
                  info( "The emulator's version, the loaded game and the emulated time: master ticks (50 MHz) and "
                        "frames since power-up." ),
                  [&emulator]( Json const& /*params*/ ) -> Outcome
                  {
                    auto const game = emulator.gameName();
                    Json status{ { "version", versionString() },
                                 { "game_name", game ? Json( *game ) : Json( nullptr ) } };
                    if ( machine::Machine const* machine = emulator.machine() )
                    {
                      status["total_ticks"] = machine::masterTicks( machine->now() );
                      status["frame"] = machine->frame();
                    }
                    return status;
                  } );

  dispatcher.add(
      "emu.load_game",
      info( "Loads a game, powering a new board up: by set name from the ROM directory (pgm for the BIOS alone), or by "
            "path to a .pgm file. Give one of the two.",
            { { .name = "name",
                .type = "string",
                .description = "A set name, such as orlegend; pgm loads the BIOS alone.",
                .required = false },
              { .name = "path", .type = "string", .description = "The path of a .pgm file.", .required = false } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        bool const byName = params.contains( "name" );
        bool const byPath = params.contains( "path" );
        if ( byName == byPath )
        {
          return std::unexpected( badRequest( "Exactly one of name and path must be given" ) );
        }
        auto const target = requireString( params, byName ? "name" : "path" );
        if ( !target )
        {
          return std::unexpected( target.error() );
        }
        auto const loaded = byName ? emulator.loadGameByName( *target ) : emulator.loadGameFromFile( *target );
        if ( !loaded )
        {
          return std::unexpected( loadError( loaded.error() ) );
        }
        return Json::object();
      } );

  dispatcher.add( "emu.cartridge_info",
                  info( "What the loaded cartridge image holds: names, hardware class, and each ROM with its type, "
                        "size, mapping and CRC32." ),
                  [&emulator]( Json const& /*params*/ ) -> Outcome
                  {
                    cart::PgmImage const* const cartridge = emulator.cartridge();
                    if ( cartridge == nullptr )
                    {
                      return std::unexpected( Error{ .code = "no_cartridge", .message = "No cartridge is loaded" } );
                    }
                    return describe( *cartridge );
                  } );

  dispatcher.alias( "sim.status", "emu.status" );
  dispatcher.alias( "sim.load_game", "emu.load_game" );
}

} // namespace pgm::control
