// The headless command line is deliberately thin: it parses arguments, wires a
// transport to the dispatcher and hands over. Everything testable lives in
// pgm_core and pgm_server.

#include "pgm/Version.hpp"
#include "pgm/control/Dispatcher.hpp"
#include "pgm/server/JsonLinesServer.hpp"

#include <CLI/CLI.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <cstdio>
#include <exception>
#include <iostream>
#include <string>

namespace
{

int run( int argc, char** argv )
{
  CLI::App app{ "Headless IGS PGM emulator", "pgmemu-cli" };
  app.set_version_flag( "--version", std::string{ pgm::versionString() } );

  bool server = false;
  app.add_flag( "--server", server, "Answer JSON-lines requests on stdin, one response per line on stdout" );

  CLI11_PARSE( app, argc, argv );

  // stdout carries the protocol and nothing else, so every log line goes to
  // stderr; a client parsing stdout must never meet one.
  spdlog::set_default_logger( spdlog::stderr_color_mt( "pgmemu" ) );

  if ( !server )
  {
    std::cerr << app.help();
    return 1;
  }

  pgm::control::Dispatcher const dispatcher;
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
