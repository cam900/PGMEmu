#include <catch2/catch_test_macros.hpp>

#include "support/Expect.hpp"
#include "support/PgmFile.hpp"

#include "pgm/cart/PgmImage.hpp"

#include <algorithm>
#include <string>
#include <vector>

using pgm::cart::Hardware;
using pgm::cart::Orientation;
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
  REQUIRE( image.version() == 0x0022 );
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

TEST_CASE( "the flags say whether the game's monitor stood on its side", "[cart]" )
{
  PgmFile file = orlegendLike();
  REQUIRE( valueOf( PgmImage::parse( pgm::test::write( file ) ) ).orientation() == Orientation::HORIZONTAL );

  file.vertical = true;
  auto bytes = pgm::test::write( file );
  REQUIRE( valueOf( PgmImage::parse( bytes ) ).orientation() == Orientation::VERTICAL );

  // A flag the reader does not know is no reason to refuse the file.
  poke32( bytes, 76, 0x80000001 );
  REQUIRE( valueOf( PgmImage::parse( bytes ) ).orientation() == Orientation::VERTICAL );
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
    poke32( bytes, 80, 10 );
    REQUIRE( errorOf( bytes ).contains( "type 10" ) );
  }

  SECTION( "a ROM type given twice" )
  {
    auto bytes = good;
    poke32( bytes, 80 + 16, 1 );
    REQUIRE( errorOf( bytes ).contains( "repeats type PRG" ) );
  }

  SECTION( "a ROM that runs past the end of the file" )
  {
    auto bytes = good;
    poke32( bytes, 80 + 12, 0x10000000 );
    REQUIRE( errorOf( bytes ).contains( "PRG entry spans" ) );
  }

  SECTION( "a ROM that starts inside the header" )
  {
    auto bytes = good;
    poke32( bytes, 80 + 8, 512 );
    REQUIRE( errorOf( bytes ).contains( "PRG entry spans" ) );
  }

  SECTION( "an empty ROM" )
  {
    auto bytes = good;
    poke32( bytes, 80 + 12, 0 );
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

TEST_CASE( "an image's own region is the one its protection holds", "[cart]" )
{
  SECTION( "ASIC3: the region block's default" )
  {
    PgmFile file = orlegendLike();
    file.regionBlock = pgm::test::asic3RegionBlock( 3, { { fourCc( "WRLD" ), 0 }, { fourCc( "CHNA" ), 3 } } );
    REQUIRE( valueOf( PgmImage::parse( pgm::test::write( file ) ) ).ownRegion() == 3U );
  }

  SECTION( "ASIC27: a big-endian word of the internal ROM" )
  {
    PgmFile file = orlegendLike();
    std::vector<std::uint8_t> internal( 0x100, 0 );
    internal[0x40] = 0x00;
    internal[0x41] = 0x05;
    file.roms.push_back( PgmRom{ .type = 2, .mapping = 0, .data = internal } );
    file.regionBlock = pgm::test::asic27RegionBlock( 0, 0x40, { { fourCc( "WRLD" ), 5 } } );
    REQUIRE( valueOf( PgmImage::parse( pgm::test::write( file ) ) ).ownRegion() == 5U );
  }

  SECTION( "ASIC27: a byte of the internal ROM, an instruction's immediate" )
  {
    PgmFile file = orlegendLike();
    std::vector<std::uint8_t> internal( 0x100, 0xe3 );
    internal[0x20] = 0x02;
    file.roms.push_back( PgmRom{ .type = 2, .mapping = 0, .data = internal } );
    file.regionBlock = pgm::test::asic27RegionBlock( 1, 0x20, { { fourCc( "JAPN" ), 2 } } );
    REQUIRE( valueOf( PgmImage::parse( pgm::test::write( file ) ) ).ownRegion() == 2U );
  }

  SECTION( "ASIC27 without an internal ROM: none" )
  {
    PgmFile file = orlegendLike();
    file.regionBlock = pgm::test::asic27RegionBlock( 0, 0x40, { { fourCc( "WRLD" ), 5 } } );
    REQUIRE_FALSE( valueOf( PgmImage::parse( pgm::test::write( file ) ) ).ownRegion().has_value() );
  }

  SECTION( "IGS025: the I25 block's default" )
  {
    PgmFile file = orlegendLike();
    file.roms.push_back(
        PgmRom{ .type = 9, .mapping = 0, .data = pgm::test::igs025Block( 2, 0x21, { { .region = 0x21 } } ) } );
    file.regionBlock = pgm::test::igs025RegionBlock( { { fourCc( "WRLD" ), 0x21 } } );
    REQUIRE( valueOf( PgmImage::parse( pgm::test::write( file ) ) ).ownRegion() == 0x21U );
  }

  SECTION( "no region block: none" )
  {
    REQUIRE_FALSE( valueOf( PgmImage::parse( pgm::test::write( orlegendLike() ) ) ).ownRegion().has_value() );
  }
}

TEST_CASE( "a region is chosen by its code, among those the image has", "[cart]" )
{
  PgmFile file = orlegendLike();
  file.roms.push_back(
      PgmRom{ .type = 9,
              .mapping = 0,
              .data = pgm::test::igs025Block( 1,
                                              6,
                                              { { .region = 6, .gameId = 0x00060006, .fill = 1 },
                                                { .region = 1, .gameId = 0x00060001, .fill = 2 },
                                                { .region = 6, .gameId = 0x00060007, .fill = 3 } } ) } );
  file.regionBlock =
      pgm::test::igs025RegionBlock( { { fourCc( "WRLD" ), 6 }, { fourCc( "JAPN" ), 1 }, { fourCc( "SNGP" ), 7 } } );
  auto const image = valueOf( PgmImage::parse( pgm::test::write( file ) ) );

  REQUIRE( image.regionValue( fourCc( "JAPN" ) ) == 1U );
  REQUIRE( image.regionValue( fourCc( "KREA" ) ).error().contains( "has no region KREA; it has WRLD, JAPN, SNGP" ) );
  // A code whose value no table names cannot be run as.
  REQUIRE( image.regionValue( fourCc( "SNGP" ) ).error().contains( "no table for region SNGP (7)" ) );

  // A value named twice is the first table's.
  auto const* const world = image.igs025Table( 6 );
  REQUIRE( world != nullptr );
  REQUIRE( world->gameId == 0x00060006U );
  REQUIRE( world->data[0] == 1 );
  REQUIRE( world->data[0xeb] == static_cast<std::uint8_t>( 1 + 0xeb ) );
  REQUIRE( image.igs025Table( 9 ) == nullptr );

  REQUIRE( valueOf( PgmImage::parse( pgm::test::write( orlegendLike() ) ) )
               .regionValue( fourCc( "WRLD" ) )
               .error()
               .contains( "no regions" ) );
}

TEST_CASE( "a four-character code spells an agnostic id", "[cart]" )
{
  REQUIRE( pgm::cart::agnosticIdOf( "WRLD" ) == fourCc( "WRLD" ) );
  REQUIRE_FALSE( pgm::cart::agnosticIdOf( "WRL" ).has_value() );
  REQUIRE_FALSE( pgm::cart::agnosticIdOf( "WORLD" ).has_value() );
}

TEST_CASE( "protection data that breaks the format is refused", "[cart]" )
{
  SECTION( "an I25 block whose size is not its tables'" )
  {
    PgmFile file = orlegendLike();
    auto block = pgm::test::igs025Block( 2, 0x21, { { .region = 0x21 } } );
    block.pop_back();
    file.roms.push_back( PgmRom{ .type = 9, .mapping = 0, .data = block } );
    REQUIRE( errorOf( pgm::test::write( file ) ).contains( "does not hold the 1 tables" ) );
  }

  SECTION( "an ASIC27 patch type the format does not define" )
  {
    PgmFile file = orlegendLike();
    file.regionBlock = pgm::test::asic27RegionBlock( 2, 0x40, { { fourCc( "WRLD" ), 5 } } );
    REQUIRE( errorOf( pgm::test::write( file ) ).contains( "patch type 2" ) );
  }
}
