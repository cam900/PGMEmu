// The desktop entry point: everything it does is Application's.

#include "Application.hpp"

#include "pgm/Version.hpp"

#include <CLI/CLI.hpp>
#include <SDL3/SDL_main.h>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>
#include <utility>

namespace
{

int run( int argc, char** argv )
{
  CLI::App app{ "IGS PGM emulator", "pgmemu" };
  app.set_version_flag( "--version", std::string{ pgm::versionString() } );
  pgm::Settings settings;
  app.add_option(
         "--bios", settings.biosSources, "A directory or zip the BIOS files are taken from; repeat to search several" )
      ->check( CLI::ExistingPath );
  app.add_option( "--rom-dir", settings.romDirectory, "Where a game given by name is found as <name>.pgm" )
      ->check( CLI::ExistingDirectory );
  app.add_option( "--state-dir", settings.stateDirectory, "Where save states given by name are kept" )
      ->check( CLI::ExistingDirectory );
  std::string server;
  app.add_option( "--server", server, "Serve JSON-lines to other programs: tcp:PORT, on 127.0.0.1" )
      ->check( []( std::string const& value )
               { return value.starts_with( "tcp:" ) ? std::string{} : std::string{ "expected tcp:PORT" }; } );
  std::uint16_t mcpPort = 0;
  app.add_option( "--mcp-http", mcpPort, "Serve MCP over HTTP on 127.0.0.1:PORT, at /mcp" );
  std::string game;
  app.add_option( "game", game, "A set name (pgm for the BIOS alone) or the path of a .pgm" );
  CLI11_PARSE( app, argc, argv );
  std::uint16_t const linePort =
      server.empty() ? std::uint16_t{ 0 } : static_cast<std::uint16_t>( std::stoul( server.substr( 4 ) ) );

  auto application = pgm::app::Application::create( std::move( settings ) );
  if ( !application )
  {
    spdlog::critical( "cannot start: {}", application.error() );
    return 1;
  }
  ( *application )->serve( linePort, mcpPort );
  if ( !game.empty() )
  {
    ( *application )->loadGame( game );
  }
  ( *application )->run();
  return 0;
}

} // namespace

// Nothing may escape main, for the reasons pgmemu-cli's main gives.
int main( int argc, char** argv )
{
  try
  {
    return run( argc, argv );
  }
  catch ( std::exception const& error )
  {
    std::fprintf( stderr, "pgmemu: %s\n", error.what() );
    return 2;
  }
  catch ( ... )
  {
    std::fputs( "pgmemu: internal error: unknown exception\n", stderr );
    return 2;
  }
}
