#include <catch2/catch_test_macros.hpp>

#include "support/Fixture.hpp"

#include "pgm/Emulator.hpp"
#include "pgm/control/Dispatcher.hpp"

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using pgm::control::Dispatcher;
using pgm::control::Json;
using pgm::test::Fixture;

namespace
{

Json call( Dispatcher const& dispatcher, std::string const& method, Json params = Json::object() )
{
  return dispatcher.handle( Json{ { "id", 1 }, { "method", method }, { "params", std::move( params ) } } );
}

std::string errorCode( Json const& response )
{
  return response.at( "error" ).at( "code" ).get<std::string>();
}

} // namespace

TEST_CASE( "input.set and input.clear hold controls, as the simulator encodes them", "[control]" )
{
  Fixture const fixture;
  pgm::Emulator emulator{ fixture.settings() };
  Dispatcher const dispatcher{ emulator };
  REQUIRE( errorCode( call( dispatcher, "input.set", { { "name", "start" } } ) ) == "not_loaded" );
  REQUIRE( call( dispatcher, "emu.load_game", { { "name", "pgm" } } ).at( "ok" ) == true );

  REQUIRE( call( dispatcher, "input.set", { { "name", "start" } } ).at( "ok" ) == true );
  REQUIRE( call( dispatcher, "input.set", { { "name", "button1" } } ).at( "ok" ) == true );
  REQUIRE( call( dispatcher, "input.get_state" ).at( "result" ).at( "buttons" ) == 0x10010 );

  REQUIRE( call( dispatcher, "input.clear", { { "name", "start" } } ).at( "ok" ) == true );
  REQUIRE( call( dispatcher, "input.get_state" ).at( "result" ).at( "buttons" ) == 0x10 );

  REQUIRE( errorCode( call( dispatcher, "input.set", { { "name", "jump" } } ) ) == "invalid_input" );
}

TEST_CASE( "input.press holds a control for two frames and lets it go for two", "[control]" )
{
  Fixture const fixture;
  pgm::Emulator emulator{ fixture.settings() };
  Dispatcher const dispatcher{ emulator };
  REQUIRE( call( dispatcher, "emu.load_game", { { "name", "pgm" } } ).at( "ok" ) == true );

  auto const result = call( dispatcher, "input.press", { { "name", "up" } } ).at( "result" );

  REQUIRE( result.at( "frames_executed" ) == 4 );
  REQUIRE( call( dispatcher, "input.get_state" ).at( "result" ).at( "buttons" ) == 0 );
}

TEST_CASE( "an audio capture is a WAV of what the runs between its start and stop produced", "[control]" )
{
  Fixture const fixture;
  pgm::Emulator emulator{ fixture.settings() };
  Dispatcher const dispatcher{ emulator };
  REQUIRE( call( dispatcher, "emu.load_game", { { "name", "pgm" } } ).at( "ok" ) == true );
  auto const path = ( fixture.romDirectory() / "capture.wav" ).string();

  REQUIRE( errorCode( call( dispatcher, "audio.capture_stop" ) ) == "capture_not_running" );
  REQUIRE( call( dispatcher, "audio.capture_start", { { "path", path } } ).at( "ok" ) == true );
  REQUIRE( errorCode( call( dispatcher, "audio.capture_start", { { "path", path } } ) ) == "capture_running" );
  REQUIRE( call( dispatcher, "emu.run_frames", { { "count", 2 } } ).at( "ok" ) == true );
  auto const stopped = call( dispatcher, "audio.capture_stop" ).at( "result" );

  // The filler BIOS never starts the ICS2115, so the WAV is empty, at the
  // rate of all 32 voices.
  REQUIRE( stopped.at( "frames" ) == 0 );
  REQUIRE( stopped.at( "sample_rate" ) == 33072 );
  std::ifstream file{ path, std::ios::binary };
  std::vector<char> const bytes{ std::istreambuf_iterator<char>{ file }, std::istreambuf_iterator<char>{} };
  REQUIRE( bytes.size() == 44 );
  REQUIRE( std::string( bytes.data(), 4 ) == "RIFF" );
  REQUIRE( std::string( bytes.data() + 8, 8 ) == "WAVEfmt " );
}

TEST_CASE( "cpu.get_state answers the Z80's registers for cpu z80", "[control]" )
{
  Fixture const fixture;
  pgm::Emulator emulator{ fixture.settings() };
  Dispatcher const dispatcher{ emulator };
  REQUIRE( call( dispatcher, "emu.load_game", { { "name", "pgm" } } ).at( "ok" ) == true );

  auto const state = call( dispatcher, "cpu.get_state", { { "cpu", "z80" } } ).at( "result" );

  // Powered up and never reset: every register zero, fetching from 0.
  REQUIRE( state.at( "pc" ) == 0 );
  REQUIRE( state.at( "sp" ) == 0 );
  REQUIRE( state.at( "halted" ) == false );
  REQUIRE( errorCode( call( dispatcher, "cpu.get_state", { { "cpu", "arm7" } } ) ) == "bad_request" );
}
