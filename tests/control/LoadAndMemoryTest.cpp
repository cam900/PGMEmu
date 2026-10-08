#include <catch2/catch_test_macros.hpp>

#include "support/Fixture.hpp"

#include "pgm/Emulator.hpp"
#include "pgm/control/Dispatcher.hpp"

#include <filesystem>
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

TEST_CASE( "a game is loaded by its set name from the ROM directory", "[control]" )
{
  Fixture const fixture;
  pgm::Emulator emulator{ fixture.settings() };
  Dispatcher const dispatcher{ emulator };

  REQUIRE( call( dispatcher, "emu.status" ).at( "result" ).at( "game_name" ).is_null() );

  REQUIRE( call( dispatcher, "emu.load_game", { { "name", "testcart" } } ).at( "ok" ) == true );

  REQUIRE( call( dispatcher, "emu.status" ).at( "result" ).at( "game_name" ) == "testcart" );
  auto const info = call( dispatcher, "emu.cartridge_info" ).at( "result" );
  REQUIRE( info.at( "short_name" ) == "testcart" );
  REQUIRE( info.at( "roms" ).at( 0 ).at( "type" ) == "PRG" );
  REQUIRE( info.at( "roms" ).at( 0 ).at( "mapping" ) == 0x100000 );
}

TEST_CASE( "a game is loaded from a path, and sim.load_game is its alias", "[control]" )
{
  Fixture const fixture;
  pgm::Emulator emulator{ fixture.settings() };
  Dispatcher const dispatcher{ emulator };
  auto const path = ( fixture.romDirectory() / "testcart.pgm" ).string();

  REQUIRE( call( dispatcher, "sim.load_game", { { "path", path } } ).at( "ok" ) == true );
  REQUIRE( emulator.gameName() == "testcart" );
}

TEST_CASE( "pgm loads the BIOS alone", "[control]" )
{
  Fixture const fixture;
  pgm::Emulator emulator{ fixture.settings() };
  Dispatcher const dispatcher{ emulator };

  REQUIRE( call( dispatcher, "emu.load_game", { { "name", "pgm" } } ).at( "ok" ) == true );

  REQUIRE( call( dispatcher, "emu.status" ).at( "result" ).at( "game_name" ) == "pgm" );
  REQUIRE( errorCode( call( dispatcher, "emu.cartridge_info" ) ) == "no_cartridge" );
  REQUIRE( call( dispatcher, "memory.list_regions" ).at( "result" ) == Json::array( { "BIOS_PROG_ROM",
                                                                                      "BIOS_TILE_ROM",
                                                                                      "BIOS_MUSIC_ROM",
                                                                                      "WORK_RAM",
                                                                                      "VIDEO_RAM",
                                                                                      "PALETTE_RAM",
                                                                                      "AUDIO_RAM" } ) );
}

TEST_CASE( "a load that fails says why, and leaves what was loaded", "[control]" )
{
  Fixture const fixture;
  pgm::Emulator emulator{ fixture.settings() };
  Dispatcher const dispatcher{ emulator };
  REQUIRE( call( dispatcher, "emu.load_game", { { "name", "testcart" } } ).at( "ok" ) == true );

  SECTION( "a set that is not in the ROM directory" )
  {
    REQUIRE( errorCode( call( dispatcher, "emu.load_game", { { "name", "nosuchgame" } } ) ) == "unknown_game" );
  }

  SECTION( "a name that is a path" )
  {
    REQUIRE( errorCode( call( dispatcher, "emu.load_game", { { "name", "../roms/testcart" } } ) ) == "unknown_game" );
  }

  SECTION( "a file that is not a .pgm" )
  {
    auto const path = ( fixture.biosDirectory() / "pgm_p02s.u20" ).string();
    REQUIRE( errorCode( call( dispatcher, "emu.load_game", { { "path", path } } ) ) == "load_failed" );
  }

  SECTION( "both a name and a path, or neither" )
  {
    REQUIRE( errorCode( call( dispatcher, "emu.load_game", { { "name", "a" }, { "path", "b" } } ) ) == "bad_request" );
    REQUIRE( errorCode( call( dispatcher, "emu.load_game" ) ) == "bad_request" );
  }

  REQUIRE( emulator.gameName() == "testcart" );
}

TEST_CASE( "no BIOS source means no game can be loaded", "[control]" )
{
  Fixture const fixture;
  pgm::Settings settings = fixture.settings();
  settings.biosSources.clear();
  pgm::Emulator emulator{ settings };
  Dispatcher const dispatcher{ emulator };

  auto const response = call( dispatcher, "emu.load_game", { { "name", "testcart" } } );

  REQUIRE( errorCode( response ) == "load_failed" );
  REQUIRE( response.at( "error" ).at( "message" ).get<std::string>().contains( "BIOS" ) );
}

TEST_CASE( "memory.read answers bytes of a region as lowercase hex", "[control]" )
{
  Fixture const fixture;
  pgm::Emulator emulator{ fixture.settings() };
  Dispatcher const dispatcher{ emulator };
  REQUIRE( call( dispatcher, "emu.load_game", { { "name", "testcart" } } ).at( "ok" ) == true );

  auto const read =
      call( dispatcher, "memory.read", { { "region", "CART_PROG_ROM" }, { "address", 0 }, { "size", 4 } } );

  REQUIRE( read.at( "result" ) == Json{ { "region", "CART_PROG_ROM" }, { "address", 0 }, { "data_hex", "4e714e75" } } );
  REQUIRE( call( dispatcher, "memory.read", { { "region", "BIOS_TILE_ROM" }, { "address", 0x1ffffe }, { "size", 2 } } )
               .at( "result" )
               .at( "data_hex" ) == "b1b1" );
}

TEST_CASE( "memory.read refuses what it cannot read", "[control]" )
{
  Fixture const fixture;
  pgm::Emulator emulator{ fixture.settings() };
  Dispatcher const dispatcher{ emulator };
  REQUIRE( call( dispatcher, "emu.load_game", { { "name", "testcart" } } ).at( "ok" ) == true );

  auto const read = [&]( std::string const& region, std::uint64_t address, std::uint64_t size )
  { return call( dispatcher, "memory.read", { { "region", region }, { "address", address }, { "size", size } } ); };

  REQUIRE( errorCode( read( "CART_ARM_ROM", 0, 1 ) ) == "invalid_region" );
  REQUIRE( errorCode( read( "CART_PROG_ROM", 2, 3 ) ) == "invalid_range" );
  REQUIRE( errorCode( read( "CART_PROG_ROM", 5, 0 ) ) == "invalid_range" );
  REQUIRE( errorCode( read( "BIOS_TILE_ROM", 0, 0x100001 ) ) == "bad_request" );
  REQUIRE( read( "CART_PROG_ROM", 4, 0 ).at( "result" ).at( "data_hex" ).get<std::string>().empty() );
}
