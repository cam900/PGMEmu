#include <catch2/catch_test_macros.hpp>

#include "support/Expect.hpp"
#include "support/PgmFile.hpp"

#include "pgm/cart/PgmImage.hpp"

#include <algorithm>
#include <string>
#include <vector>

using pgm::cart::Hardware;
using pgm::cart::PgmImage;
using pgm::cart::RegionScheme;
using pgm::cart::RomType;
using pgm::test::fourCc;
using pgm::test::PgmFile;
using pgm::test::PgmRom;
using pgm::test::poke32;

using pgm::test::valueOf;

namespace
{

std::vector<std::uint8_t> bytesOf( std::size_t size, std::uint8_t seed )
{
  std::vector<std::uint8_t> bytes( size );
  for ( std::size_t i = 0; i < size; ++i )
  {
    bytes[i] = static_cast<std::uint8_t>( seed + ( i * 7 ) );
  }
  return bytes;
}

PgmFile orlegendLike()
{
  PgmFile file;
  file.shortName = "orlegend";
  file.hardware = 1;
  file.roms = { PgmRom{ .type = 1, .mapping = 0x100000, .data = bytesOf( 0x1000, 1 ) },
                PgmRom{ .type = 4, .mapping = 0x180000, .data = bytesOf( 0x800, 2 ) },
                PgmRom{ .type = 6, .mapping = 0, .data = bytesOf( 0x600, 3 ) },
                PgmRom{ .type = 7, .mapping = 0x400000, .data = bytesOf( 0x200, 4 ) } };
  return file;
}

std::string errorOf( std::vector<std::uint8_t> bytes )
{
  auto const image = PgmImage::parse( std::move( bytes ) );
  REQUIRE_FALSE( image.has_value() );
  return image.error();
}

} // namespace

TEST_CASE( "a .pgm is read: its header fields and every ROM, in file order", "[cart]" )
{
  PgmFile const file = orlegendLike();

  auto const image = valueOf( PgmImage::parse( pgm::test::write( file ) ) );
  REQUIRE( image.version() == 0x0021 );
  REQUIRE( image.shortName() == "orlegend" );
  REQUIRE( image.year() == "1997" );
  REQUIRE( image.manufacturer() == "IGS" );
  REQUIRE( image.longName() == "Test Cartridge" );
  REQUIRE( image.hardware() == Hardware::ASIC3 );
  REQUIRE_FALSE( image.regionInfo().has_value() );

  auto const roms = image.roms();
  REQUIRE( roms.size() == file.roms.size() );
  for ( std::size_t i = 0; i < roms.size(); ++i )
  {
    REQUIRE( std::to_underlying( roms[i].type ) == file.roms[i].type );
    REQUIRE( roms[i].mapping == file.roms[i].mapping );
    REQUIRE( std::ranges::equal( roms[i].data, file.roms[i].data ) );
  }
  REQUIRE( valueOf( image.rom( RomType::TLE ) ).mapping == 0x180000 );
  REQUIRE_FALSE( image.rom( RomType::EXT ).has_value() );
}

TEST_CASE( "a short name that fills its 16 bytes needs no NUL", "[cart]" )
{
  PgmFile file = orlegendLike();
  file.shortName = "sixteen_chars_xx";

  auto const image = valueOf( PgmImage::parse( pgm::test::write( file ) ) );
  REQUIRE( image.shortName() == "sixteen_chars_xx" );
}

TEST_CASE( "an ASIC3 region block is read with its default and its table", "[cart]" )
{
  PgmFile file = orlegendLike();
  file.regionBlock = pgm::test::asic3RegionBlock( 0, { { fourCc( "WRLD" ), 0 }, { fourCc( "CHNA" ), 3 } } );

  auto const image = valueOf( PgmImage::parse( pgm::test::write( file ) ) );
  auto const info = valueOf( image.regionInfo() );
  REQUIRE( info.scheme == RegionScheme::ASIC3 );
  REQUIRE( info.defaultRegion == 0 );
  REQUIRE( info.regions.size() == 2 );
  REQUIRE( pgm::cart::fourCc( info.regions[1].agnosticId ) == "CHNA" );
  REQUIRE( info.regions[1].regionId == 3 );
}

TEST_CASE( "a file that breaks the format is refused with the field at fault named", "[cart]" )
{
  std::vector<std::uint8_t> const good = pgm::test::write( orlegendLike() );

  SECTION( "shorter than the header" )
  {
    REQUIRE( errorOf( std::vector<std::uint8_t>( 100, 0 ) ).contains( "shorter" ) );
  }

  SECTION( "the magic" )
  {
    auto bytes = good;
    bytes[0] = 'X';
    REQUIRE( errorOf( bytes ).contains( "magic" ) );
  }

  SECTION( "another version" )
  {
    auto bytes = good;
    bytes[7] = 0x20;
    REQUIRE( errorOf( bytes ).contains( "version 0020" ) );
  }

  SECTION( "an info block of another size" )
  {
    auto bytes = good;
    poke32( bytes, 8, 84 );
    REQUIRE( errorOf( bytes ).contains( "infoSize" ) );
  }

  SECTION( "an entry table that runs past the header" )
  {
    auto bytes = good;
    poke32( bytes, 56, 100 );
    REQUIRE( errorOf( bytes ).contains( "entry table" ) );
  }

  SECTION( "a ROM type the format does not define" )
  {
    auto bytes = good;
    poke32( bytes, 76, 10 );
    REQUIRE( errorOf( bytes ).contains( "type 10" ) );
  }

  SECTION( "a ROM type given twice" )
  {
    auto bytes = good;
    poke32( bytes, 76 + 16, 1 );
    REQUIRE( errorOf( bytes ).contains( "repeats type PRG" ) );
  }

  SECTION( "a ROM that runs past the end of the file" )
  {
    auto bytes = good;
    poke32( bytes, 76 + 12, 0x10000000 );
    REQUIRE( errorOf( bytes ).contains( "PRG entry spans" ) );
  }

  SECTION( "a ROM that starts inside the header" )
  {
    auto bytes = good;
    poke32( bytes, 76 + 8, 512 );
    REQUIRE( errorOf( bytes ).contains( "PRG entry spans" ) );
  }

  SECTION( "an empty ROM" )
  {
    auto bytes = good;
    poke32( bytes, 76 + 12, 0 );
    REQUIRE( errorOf( bytes ).contains( "PRG entry is empty" ) );
  }

  SECTION( "a string that is not terminated within the header" )
  {
    auto bytes = good;
    std::fill( bytes.begin() + 1000, bytes.begin() + 1024, std::uint8_t{ 'x' } );
    poke32( bytes, 28, 1000 );
    REQUIRE( errorOf( bytes ).contains( "manufacturerLongName" ) );
  }

  SECTION( "both an ASCII and a UTF-8 long name" )
  {
    auto bytes = good;
    auto const asciiAt = bytes[32] | ( bytes[33] << 8U );
    poke32( bytes, 36, static_cast<std::uint32_t>( asciiAt ) );
    REQUIRE( errorOf( bytes ).contains( "at most one" ) );
  }

  SECTION( "a region block of an unknown kind" )
  {
    PgmFile file = orlegendLike();
    file.regionBlock = pgm::test::asic3RegionBlock( 0, { { fourCc( "WRLD" ), 0 } } );
    auto bytes = pgm::test::write( file );
    auto const regionAt = bytes[72] | ( bytes[73] << 8U );
    bytes[static_cast<std::size_t>( regionAt )] = 7;
    REQUIRE( errorOf( bytes ).contains( "unknown type 7" ) );
  }
}
