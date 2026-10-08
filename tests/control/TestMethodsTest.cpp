#include <catch2/catch_test_macros.hpp>

#include "support/Fixture.hpp"

#include "pgm/Emulator.hpp"
#include "pgm/control/Dispatcher.hpp"

#include <fstream>
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

constexpr std::size_t LINK_AT = 0x100;

/// Work RAM, in 68000 byte order, with what a test ROM leaves there: a debug
/// link block at LINK_AT whose outgoing ring holds `sent`, and a status block
/// "VT" with two words.
std::vector<char> testRam( std::string const& sent )
{
  std::vector<char> ram( 0x20000, 0 );
  for ( std::size_t i = 0; i < 4; ++i )
  {
    ram[LINK_AT + i] = "RFIF"[i];
  }
  ram[LINK_AT + 5] = 1;                                 // ready
  ram[LINK_AT + 11] = static_cast<char>( sent.size() ); // out_head
  for ( std::size_t i = 0; i < sent.size(); ++i )
  {
    ram[LINK_AT + 14 + 512 + i] = sent[i];
  }
  ram[0x1f000] = 'V';
  ram[0x1f001] = 'T';
  ram[0x1f003] = 7;
  return ram;
}

/// Loads `ram` into work RAM through nvram.load, whose files hold each word's
/// low byte first.
void loadRam( Dispatcher const& dispatcher, std::filesystem::path const& path, std::vector<char> const& ram )
{
  std::vector<char> file( ram.size() );
  for ( std::size_t i = 0; i < ram.size(); ++i )
  {
    file[i] = ram[i ^ 1U];
  }
  {
    std::ofstream out{ path, std::ios::binary };
    out.write( file.data(), static_cast<std::streamsize>( file.size() ) );
  }
  REQUIRE( call( dispatcher, "nvram.load", { { "filename", path.string() } } ).at( "ok" ) == true );
}

std::string workRamHex( Dispatcher const& dispatcher, std::size_t address, std::size_t size )
{
  return call( dispatcher, "memory.read", { { "region", "WORK_RAM" }, { "address", address }, { "size", size } } )
      .at( "result" )
      .at( "data_hex" )
      .get<std::string>();
}

} // namespace

TEST_CASE( "the debug link writes into the test ROM's ring and reads from its own", "[control][test]" )
{
  Fixture const fixture;
  pgm::Emulator emulator{ fixture.settings() };
  Dispatcher const dispatcher{ emulator };
  REQUIRE( call( dispatcher, "emu.load_game", { { "name", "pgm" } } ).at( "ok" ) == true );
  loadRam( dispatcher, fixture.romDirectory() / "ram.nv", testRam( "hi!" ) );

  REQUIRE( call( dispatcher, "debug_link.start" ).at( "ok" ) == true );
  // Attached: the block is marked active.
  REQUIRE( workRamHex( dispatcher, LINK_AT + 4, 1 ) == "01" );

  REQUIRE( call( dispatcher, "debug_link.write", { { "data_hex", "0102" } } ).at( "ok" ) == true );
  REQUIRE( workRamHex( dispatcher, LINK_AT + 6, 2 ) == "0002" );
  REQUIRE( workRamHex( dispatcher, LINK_AT + 14, 2 ) == "0102" );

  auto const first = call( dispatcher, "debug_link.read", { { "max_bytes", 2 } } ).at( "result" );
  REQUIRE( first.at( "data_hex" ) == "6869" );
  REQUIRE( first.at( "available" ) == 1 );
  REQUIRE( workRamHex( dispatcher, LINK_AT + 12, 2 ) == "0003" );
  REQUIRE( call( dispatcher, "debug_link.read", { { "max_bytes", 8 } } ).at( "result" ).at( "data_hex" ) == "21" );

  REQUIRE( call( dispatcher, "debug_link.write", { { "data_hex", "zz" } } ).at( "error" ).at( "code" ) ==
           "bad_request" );
  REQUIRE( call( dispatcher, "debug_link.stop" ).at( "ok" ) == true );
  REQUIRE( workRamHex( dispatcher, LINK_AT + 4, 1 ) == "00" );
}

TEST_CASE( "a debug link the test ROM never published times out", "[control][test]" )
{
  Fixture const fixture;
  pgm::Emulator emulator{ fixture.settings() };
  Dispatcher const dispatcher{ emulator };
  REQUIRE( call( dispatcher, "emu.load_game", { { "name", "pgm" } } ).at( "ok" ) == true );

  auto const answer =
      call( dispatcher, "debug_link.write", { { "data_hex", "01" }, { "timeout_cycles_per_byte", 100000 } } );
  REQUIRE( answer.at( "error" ).at( "code" ) == "debug_link_timeout" );
  REQUIRE( call( dispatcher, "debug_link.read", { { "max_bytes", 4 } } )
               .at( "result" )
               .at( "data_hex" )
               .get<std::string>()
               .empty() );
}

TEST_CASE( "test.status answers PGMTest's status block", "[control][test]" )
{
  Fixture const fixture;
  pgm::Emulator emulator{ fixture.settings() };
  Dispatcher const dispatcher{ emulator };
  REQUIRE( call( dispatcher, "emu.load_game", { { "name", "pgm" } } ).at( "ok" ) == true );
  loadRam( dispatcher, fixture.romDirectory() / "ram.nv", testRam( "" ) );

  auto const status = call( dispatcher, "test.status", { { "words", 2 } } ).at( "result" );

  REQUIRE( status.at( "address" ) == 0x81f000 );
  REQUIRE( status.at( "name" ) == "VT" );
  REQUIRE( status.at( "words" ) == Json::array( { 0x5654, 7 } ) );
}
