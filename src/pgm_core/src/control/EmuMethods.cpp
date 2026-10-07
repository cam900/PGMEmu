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
                  [&emulator]( Json const& /*params*/ ) -> Outcome
                  {
                    auto const game = emulator.gameName();
                    return Json{ { "version", versionString() },
                                 { "game_name", game ? Json( *game ) : Json( nullptr ) } };
                  } );

  dispatcher.add( "emu.load_game",
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
                    auto const loaded =
                        byName ? emulator.loadGameByName( *target ) : emulator.loadGameFromFile( *target );
                    if ( !loaded )
                    {
                      return std::unexpected( loadError( loaded.error() ) );
                    }
                    return Json::object();
                  } );

  dispatcher.add( "emu.cartridge_info",
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
