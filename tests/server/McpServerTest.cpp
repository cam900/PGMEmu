#include <catch2/catch_test_macros.hpp>

#include "support/Expect.hpp"
#include "support/Fixture.hpp"

#include "pgm/Emulator.hpp"
#include "pgm/control/Dispatcher.hpp"
#include "pgm/server/McpServer.hpp"

#include <sstream>
#include <string>

using pgm::control::Json;
using pgm::server::McpServer;

namespace
{

Json request( int id, std::string const& method, Json params = Json::object() )
{
  return Json{ { "jsonrpc", "2.0" }, { "id", id }, { "method", method }, { "params", std::move( params ) } };
}

Json call( McpServer& server, std::string const& tool, Json arguments = Json::object() )
{
  return pgm::test::valueOf( server.handle( request(
                                 7, "tools/call", { { "name", tool }, { "arguments", std::move( arguments ) } } ) ) )
      .at( "result" );
}

} // namespace

TEST_CASE( "initialize answers the protocol revision asked for, and the server's name", "[server][mcp]" )
{
  pgm::test::Fixture const fixture;
  pgm::Emulator emulator{ fixture.settings() };
  pgm::control::Dispatcher const dispatcher{ emulator };
  McpServer server{ dispatcher };

  auto const answer =
      pgm::test::valueOf( server.handle( request( 1, "initialize", { { "protocolVersion", "2025-06-18" } } ) ) );
  REQUIRE( answer.at( "id" ) == 1 );
  REQUIRE( answer.at( "result" ).at( "protocolVersion" ) == "2025-06-18" );
  REQUIRE( answer.at( "result" ).at( "serverInfo" ).at( "name" ) == "pgmemu" );
  REQUIRE( answer.at( "result" ).at( "capabilities" ).contains( "tools" ) );

  auto const unknown =
      pgm::test::valueOf( server.handle( request( 2, "initialize", { { "protocolVersion", "1999-01-01" } } ) ) );
  REQUIRE( unknown.at( "result" ).at( "protocolVersion" ) == "2025-11-25" );

  // A notification is answered by nothing.
  REQUIRE_FALSE( server.handle( Json{ { "jsonrpc", "2.0" }, { "method", "notifications/initialized" } } ) );
}

TEST_CASE( "the tools are the dispatcher's methods, aliases left out, with their schemas", "[server][mcp]" )
{
  pgm::test::Fixture const fixture;
  pgm::Emulator emulator{ fixture.settings() };
  pgm::control::Dispatcher const dispatcher{ emulator };
  McpServer server{ dispatcher };

  auto const tools = pgm::test::valueOf( server.handle( request( 1, "tools/list" ) ) ).at( "result" ).at( "tools" );
  REQUIRE( tools.size() == dispatcher.methods().size() );
  bool found = false;
  for ( auto const& tool : tools )
  {
    REQUIRE_FALSE( tool.at( "name" ).get<std::string>().starts_with( "sim_" ) );
    REQUIRE( tool.at( "inputSchema" ).at( "type" ) == "object" );
    if ( tool.at( "name" ) == "emu_run_frames" )
    {
      found = true;
      REQUIRE( tool.at( "inputSchema" ).at( "required" ) == Json::array( { "count" } ) );
      REQUIRE( tool.at( "inputSchema" ).at( "properties" ).at( "count" ).at( "type" ) == "integer" );
    }
  }
  REQUIRE( found );
}

TEST_CASE( "a tool call runs its method; a failure is a flagged result, an unknown tool an error", "[server][mcp]" )
{
  pgm::test::Fixture const fixture;
  pgm::Emulator emulator{ fixture.settings() };
  pgm::control::Dispatcher const dispatcher{ emulator };
  McpServer server{ dispatcher };

  auto const failed = call( server, "emu_run_frames", { { "count", 1 } } );
  REQUIRE( failed.at( "isError" ) == true );
  REQUIRE( failed.at( "content" ).at( 0 ).at( "text" ).get<std::string>().starts_with( "not_loaded" ) );

  REQUIRE( call( server, "emu_load_game", { { "name", "pgm" } } ).at( "isError" ) == false );
  auto const ran = call( server, "emu_run_frames", { { "count", 1 } } );
  REQUIRE( Json::parse( ran.at( "content" ).at( 0 ).at( "text" ).get<std::string>() ).at( "frames_executed" ) == 1 );

  auto const unknown = pgm::test::valueOf(
      server.handle( request( 3, "tools/call", { { "name", "emu_fly" }, { "arguments", Json::object() } } ) ) );
  REQUIRE( unknown.at( "error" ).at( "code" ) == -32602 );
}

TEST_CASE( "a screenshot comes back as an image", "[server][mcp]" )
{
  pgm::test::Fixture const fixture;
  pgm::Emulator emulator{ fixture.settings() };
  pgm::control::Dispatcher const dispatcher{ emulator };
  McpServer server{ dispatcher };
  REQUIRE( call( server, "emu_load_game", { { "name", "pgm" } } ).at( "isError" ) == false );

  auto const content = call( server, "video_screenshot" ).at( "content" );

  REQUIRE( content.at( 0 ).at( "type" ) == "image" );
  REQUIRE( content.at( 0 ).at( "mimeType" ) == "image/png" );
  REQUIRE( content.at( 0 ).at( "data" ).get<std::string>().starts_with( "iVBORw0KGgo" ) );
  REQUIRE( Json::parse( content.at( 1 ).at( "text" ).get<std::string>() ).at( "width" ) == 448 );
}

TEST_CASE( "the stdio transport answers a line per request, and nothing to a notification", "[server][mcp]" )
{
  pgm::test::Fixture const fixture;
  pgm::Emulator emulator{ fixture.settings() };
  pgm::control::Dispatcher const dispatcher{ emulator };
  McpServer server{ dispatcher };
  std::istringstream in{ R"({"jsonrpc":"2.0","method":"notifications/initialized"})"
                         "\n"
                         R"({"jsonrpc":"2.0","id":"a","method":"ping"})"
                         "\nnot json\n" };
  std::ostringstream out;

  server.serve( in, out );

  std::istringstream lines{ out.str() };
  std::string line;
  REQUIRE( std::getline( lines, line ) );
  REQUIRE( Json::parse( line ).at( "id" ) == "a" );
  REQUIRE( std::getline( lines, line ) );
  REQUIRE( Json::parse( line ).at( "error" ).at( "code" ) == -32700 );
  REQUIRE_FALSE( std::getline( lines, line ) );
}
