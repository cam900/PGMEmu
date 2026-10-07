#include <catch2/catch_test_macros.hpp>

#include "support/Expect.hpp"
#include "support/Files.hpp"

#include "pgm/io/RomSources.hpp"

#include <array>
#include <filesystem>
#include <string_view>
#include <vector>

using pgm::io::RomSources;
using pgm::test::TemporaryDirectory;

using pgm::test::valueOf;

namespace
{

std::vector<std::uint8_t> text( std::string_view value )
{
  return { value.begin(), value.end() };
}

} // namespace

TEST_CASE( "crc32 is the CRC-32 ROM sets are identified by", "[io]" )
{
  // The check value every CRC-32/ISO-HDLC implementation agrees on.
  REQUIRE( pgm::io::crc32( text( "123456789" ) ) == 0xcbf43926 );
}

TEST_CASE( "a file is taken from the first place that has it", "[io]" )
{
  TemporaryDirectory const scratch;
  auto const directory = scratch.path() / "override";
  std::filesystem::create_directory( directory );
  pgm::test::writeFile( directory / "a.bin", text( "from the directory" ) );
  auto const zip = scratch.path() / "set.zip";
  pgm::test::writeZip( zip, { { "a.bin", text( "from the zip" ) }, { "b.bin", text( "only in the zip" ) } } );

  std::array const places{ directory, zip };
  auto const sources = valueOf( RomSources::open( places ) );
  REQUIRE( sources.find( "a.bin" ) == text( "from the directory" ) );
  REQUIRE( sources.origin( "a.bin" ) == directory );
  REQUIRE( sources.find( "b.bin" ) == text( "only in the zip" ) );
  REQUIRE( sources.origin( "b.bin" ) == zip );
  REQUIRE_FALSE( sources.find( "c.bin" ).has_value() );
}

TEST_CASE( "a place that is neither a directory nor a zip is refused", "[io]" )
{
  TemporaryDirectory const scratch;
  auto const notZip = scratch.path() / "notes.txt";
  pgm::test::writeFile( notZip, text( "not an archive" ) );

  std::array const missing{ scratch.path() / "nowhere" };
  std::array const notArchive{ notZip };

  REQUIRE( RomSources::open( missing ).error().contains( "neither a directory nor a file" ) );
  REQUIRE( RomSources::open( notArchive ).error().contains( "cannot be opened as a zip" ) );
}
