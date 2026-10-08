// The headless command line is deliberately thin: it parses arguments, wires a
// transport to the dispatcher and hands over. Everything testable lives in
// pgm_core and pgm_server.

#include "pgm/Emulator.hpp"
#include "pgm/Version.hpp"
#include "pgm/cart/PgmImage.hpp"
#include "pgm/control/Describe.hpp"
#include "pgm/control/Dispatcher.hpp"
#include "pgm/server/JsonLinesServer.hpp"
#include "pgm/server/McpServer.hpp"
#include "pgm/server/TcpLineServer.hpp"

#include <CLI/CLI.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{

/// Prints what `emu.cartridge_info` would answer about the file at `path`.
int printInfo( std::filesystem::path const& path )
{
  auto const image = pgm::cart::PgmImage::read( path );
  if ( !image )
  {
    std::cerr << "pgmemu-cli: " << path.string() << ": " << image.error() << '\n';
    return 1;
  }
  std::cout << pgm::control::describe( *image ).dump( 2 ) << '\n';
  return 0;
}

int run( int argc, char** argv )
{
  CLI::App app{ "Headless IGS PGM emulator", "pgmemu-cli" };
  app.set_version_flag( "--version", std::string{ pgm::versionString() } );

  pgm::Settings settings;
  app.add_option( "--bios",
                  settings.biosSources,
                  "A directory or zip the BIOS files are taken from; repeat to search several, first first" )
      ->check( CLI::ExistingPath );
  app.add_option( "--rom-dir", settings.romDirectory, "Where emu.load_game finds <name>.pgm" )
      ->check( CLI::ExistingDirectory );
  app.add_option( "--state-dir",
                  settings.stateDirectory,
                  "Where state.save and state.load keep save states given by name; the working directory if not given" )
      ->check( CLI::ExistingDirectory );

  std::string serverSpec;
  auto* const serverFlag =
      app.add_option( "--server",
                      serverSpec,
                      "Answer JSON-lines requests: on stdin and stdout, or with tcp:PORT on 127.0.0.1:PORT until "
                      "the process is ended" )
          ->expected( 0, 1 )
          ->check( []( std::string const& value )
                   { return value.starts_with( "tcp:" ) ? std::string{} : std::string{ "expected tcp:PORT" }; } );
  bool mcp = false;
  auto* const mcpFlag =
      app.add_flag( "--mcp", mcp, "Serve the Model Context Protocol on stdio, for an agent" )->excludes( serverFlag );
  std::filesystem::path info;
  app.add_option( "--info", info, "Describe a .pgm file and exit" )
      ->check( CLI::ExistingFile )
      ->excludes( serverFlag )
      ->excludes( mcpFlag );

  CLI11_PARSE( app, argc, argv );

  if ( !info.empty() )
  {
    return printInfo( info );
  }

  // stdout carries the protocol and nothing else, so every log line goes to
  // stderr; a client parsing stdout must never meet one.
  spdlog::set_default_logger( spdlog::stderr_color_mt( "pgmemu" ) );

  bool const server = serverFlag->count() > 0;
  if ( !server && !mcp )
  {
    std::cerr << app.help();
    return 1;
  }

  pgm::Emulator emulator{ std::move( settings ) };
  pgm::control::Dispatcher const dispatcher{ emulator };
  if ( mcp )
  {
    pgm::server::McpServer transport{ dispatcher };
    spdlog::info( "pgmemu-cli {} serving MCP on stdio", pgm::versionString() );
    transport.serve( std::cin, std::cout );
    return 0;
  }
  if ( !serverSpec.empty() )
  {
    // Connections are served on threads of their own; the dispatcher answers
    // one request at a time.
    std::mutex oneAtATime;
    pgm::server::TcpLineServer const transport{ static_cast<std::uint16_t>( std::stoul( serverSpec.substr( 4 ) ) ),
                                                [&]( pgm::control::Json const& request )
                                                {
                                                  std::scoped_lock const lock{ oneAtATime };
                                                  return dispatcher.handle( request );
                                                } };
    spdlog::info( "pgmemu-cli {} serving JSON-lines on 127.0.0.1:{}", pgm::versionString(), transport.port() );
    for ( ;; )
    {
      std::this_thread::sleep_for( std::chrono::hours{ 1 } );
    }
  }
  pgm::server::JsonLinesServer const transport{ dispatcher };
  spdlog::info( "pgmemu-cli {} serving JSON-lines on stdio", pgm::versionString() );
  transport.serve( std::cin, std::cout );
  return 0;
}

} // namespace

// Nothing may escape main: an unhandled exception would abort without telling
// the user anything useful, and CLI11 reports argument problems by throwing.
// The handlers write to stderr directly rather than through spdlog, because
// logging may allocate or throw and would then be the second failure in a row.
int main( int argc, char** argv )
{
  try
  {
    return run( argc, argv );
  }
  catch ( std::exception const& error )
  {
    std::fprintf( stderr, "pgmemu-cli: %s\n", error.what() );
    return 2;
  }
  catch ( ... )
  {
    std::fputs( "pgmemu-cli: internal error: unknown exception\n", stderr );
    return 2;
  }
}
