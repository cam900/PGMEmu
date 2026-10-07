#include <catch2/catch_test_macros.hpp>

#include "support/Expect.hpp"
#include "support/Files.hpp"

#include "pgm/cart/Bios.hpp"

#include <array>
#include <filesystem>
#include <vector>

using pgm::cart::Bios;
using pgm::io::RomSources;
using pgm::test::TemporaryDirectory;

using pgm::test::valueOf;

namespace
{

constexpr std::size_t PROGRAM_SIZE = 0x20000;
constexpr std::size_t DATA_SIZE = 0x200000;

std::vector<std::uint8_t> filled( std::size_t size, std::uint8_t value )
{
  // Not `return { size, value }`: braces would make a vector of those two bytes.
  std::vector<std::uint8_t> bytes( size, value );
  return bytes;
}

} // namespace

TEST_CASE( "the BIOS is its three files, each taken from the first source that has it", "[cart]" )
{
  TemporaryDirectory const scratch;
  auto const testBios = scratch.path() / "test";
  std::filesystem::create_directory( testBios );
  pgm::test::writeFile( testBios / "pgm_p02s.u20", filled( PROGRAM_SIZE, 0x11 ) );
  auto const zip = scratch.path() / "pgm.zip";
  pgm::test::writeZip( zip,
                       { { "pgm_p02s.u20", filled( PROGRAM_SIZE, 0x22 ) },
                         { "pgm_t01s.rom", filled( DATA_SIZE, 0x33 ) },
                         { "pgm_m01s.rom", filled( DATA_SIZE, 0x44 ) } } );

  std::array const places{ testBios, zip };
  auto const bios = valueOf( Bios::load( valueOf( RomSources::open( places ) ) ) );
  REQUIRE( bios.program().data == filled( PROGRAM_SIZE, 0x11 ) );
  REQUIRE( bios.program().origin == testBios );
  REQUIRE( bios.tiles().data == filled( DATA_SIZE, 0x33 ) );
  REQUIRE( bios.music().data == filled( DATA_SIZE, 0x44 ) );
  // None of these is MAME's dump; they load all the same and say so.
  REQUIRE_FALSE( bios.program().known );
}

TEST_CASE( "a missing or wrongly sized BIOS file is named", "[cart]" )
{
  TemporaryDirectory const scratch;
  pgm::test::writeFile( scratch.path() / "pgm_p02s.u20", filled( PROGRAM_SIZE, 0 ) );
  std::array const places{ scratch.path() };

  SECTION( "missing" )
  {
    REQUIRE( Bios::load( valueOf( RomSources::open( places ) ) ).error().contains( "pgm_t01s.rom" ) );
  }

  SECTION( "the wrong size" )
  {
    pgm::test::writeFile( scratch.path() / "pgm_t01s.rom", filled( 16, 0 ) );
    auto const error = Bios::load( valueOf( RomSources::open( places ) ) ).error();
    REQUIRE( error.contains( "pgm_t01s.rom holds 16 bytes" ) );
  }
}
