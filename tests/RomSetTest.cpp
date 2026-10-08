#include <catch2/catch_test_macros.hpp>

#include "pgm/cart/Bios.hpp"
#include "pgm/cart/PgmImage.hpp"
#include "pgm/io/RomSources.hpp"
#include "pgm/machine/Machine.hpp"
#include "support/Expect.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <span>
#include <string>
#include <tuple>
#include <vector>

// These hold the reader against the real thing: the MAME sets in the workspace
// and the images scripts/make-pgm.sh builds from them. Neither is in the tree,
// so a checkout without them skips these rather than failing them.

using pgm::cart::PgmImage;
using pgm::cart::RomType;
using pgm::io::RomSources;

using pgm::test::valueOf;

namespace
{

// Functions rather than constants: a path built during static initialisation
// may throw where nothing can catch it.
std::filesystem::path pgmDirectory()
{
  return PGM_TEST_PGM_DIR;
}

std::filesystem::path romsDirectory()
{
  return PGM_TEST_ROMS_DIR;
}

std::vector<std::filesystem::path> pgmFiles()
{
  std::vector<std::filesystem::path> files;
  std::error_code failed;
  for ( auto const& entry : std::filesystem::directory_iterator( pgmDirectory(), failed ) )
  {
    if ( entry.path().extension() == ".pgm" )
    {
      files.push_back( entry.path() );
    }
  }
  std::ranges::sort( files );
  return files;
}

/// The files of a MAME set laid end to end, as PGMBuilder assembles a region
/// whose ROMs are loaded one after another with nothing to decrypt.
std::vector<std::uint8_t> concatenated( RomSources const& set, std::vector<char const*> const& names )
{
  std::vector<std::uint8_t> bytes;
  for ( char const* name : names )
  {
    auto const file = valueOf( set.find( name ) );
    bytes.insert( bytes.end(), file.begin(), file.end() );
  }
  return bytes;
}

} // namespace

TEST_CASE( "every image built from the workspace's ROM sets is read", "[roms]" )
{
  auto const files = pgmFiles();
  if ( files.empty() )
  {
    SKIP( "no .pgm files in " << pgmDirectory() << "; build them with scripts/make-pgm.sh" );
  }

  for ( auto const& path : files )
  {
    INFO( path.filename() );
    // CHECK rather than REQUIRE, so that one bad image does not hide the rest.
    auto const image = PgmImage::read( path );
    INFO( image.error_or( std::string{} ) );
    CHECK( image.has_value() );
    CHECK( image.transform( []( PgmImage const& read ) { return read.shortName(); } ) == path.stem().string() );
    CHECK( image.transform( []( PgmImage const& read ) { return read.rom( RomType::PRG ).has_value(); } ) == true );
  }
}

TEST_CASE( "orlegend's ROMs are the files of orlegend.zip", "[roms]" )
{
  auto const pgmPath = pgmDirectory() / "orlegend.pgm";
  auto const zipPath = romsDirectory() / "orlegend.zip";
  if ( !std::filesystem::exists( pgmPath ) || !std::filesystem::exists( zipPath ) )
  {
    SKIP( "needs " << pgmPath << " and " << zipPath );
  }

  auto const image = valueOf( PgmImage::read( pgmPath ) );
  std::array const places{ zipPath };
  auto const set = valueOf( RomSources::open( places ) );

  // orlegend's program is not encrypted, and PGMBuilder stores a program as
  // its file holds it, so each region is its files byte for byte.
  REQUIRE( std::ranges::equal( valueOf( image.rom( RomType::PRG ) ).data, concatenated( set, { "p0103.rom" } ) ) );
  REQUIRE( std::ranges::equal( valueOf( image.rom( RomType::TLE ) ).data, concatenated( set, { "t0100.u8" } ) ) );
  REQUIRE( std::ranges::equal( valueOf( image.rom( RomType::AUD ) ).data, concatenated( set, { "m0100.u1" } ) ) );
  REQUIRE( std::ranges::equal(
      valueOf( image.rom( RomType::SPC ) ).data,
      concatenated( set, { "a0100.u5", "a0101.u6", "a0102.u7", "a0103.u8", "a0104.u11", "a0105.u12" } ) ) );
  REQUIRE( std::ranges::equal( valueOf( image.rom( RomType::SPM ) ).data,
                               concatenated( set, { "b0100.u9", "b0101.u10", "b0102.u15" } ) ) );
  REQUIRE( valueOf( image.rom( RomType::PRG ) ).mapping == 0x100000 );
  REQUIRE( valueOf( image.rom( RomType::TLE ) ).mapping == 0x180000 );
  REQUIRE( valueOf( image.rom( RomType::AUD ) ).mapping == 0x400000 );
}

TEST_CASE( "the BIOS of pgm.zip is MAME's dump", "[roms]" )
{
  auto const zipPath = romsDirectory() / "pgm.zip";
  if ( !std::filesystem::exists( zipPath ) )
  {
    SKIP( "needs " << zipPath );
  }

  std::array const places{ zipPath };
  auto const bios = valueOf( pgm::cart::Bios::load( valueOf( RomSources::open( places ) ) ) );
  REQUIRE( bios.program().known );
  REQUIRE( bios.tiles().known );
  REQUIRE( bios.music().known );
}

TEST_CASE( "the BIOS starts its sound driver and plays", "[roms]" )
{
  auto const zipPath = romsDirectory() / "pgm.zip";
  if ( !std::filesystem::exists( zipPath ) )
  {
    SKIP( "needs " << zipPath );
  }

  std::array const places{ zipPath };
  auto const bios = valueOf( pgm::cart::Bios::load( valueOf( RomSources::open( places ) ) ) );
  pgm::machine::Machine machine{ bios, nullptr };
  std::size_t frames = 0;
  std::size_t sounding = 0;
  for ( int frame = 0; frame < 120; ++frame )
  {
    machine.runFrames( 1 );
    frames += machine.audio().size();
    sounding += static_cast<std::size_t>( std::ranges::count_if(
        machine.audio(), []( pgm::machine::AudioFrame const& f ) { return f.left != 0 || f.right != 0; } ) );
  }

  // Two seconds at 33 kHz, from the ICS2115 the Z80 started, and some of it
  // the jingle.
  REQUIRE( frames > 60'000 );
  REQUIRE( sounding > 10'000 );
  REQUIRE( machine.z80Registers().im == 1 );
}

TEST_CASE( "a save state taken mid-run gives back the same frames, memory and sound", "[roms]" )
{
  auto const zipPath = romsDirectory() / "pgm.zip";
  if ( !std::filesystem::exists( zipPath ) )
  {
    SKIP( "needs " << zipPath );
  }

  std::array const places{ zipPath };
  auto const bios = valueOf( pgm::cart::Bios::load( valueOf( RomSources::open( places ) ) ) );
  pgm::machine::Machine machine{ bios, nullptr };
  machine.runFrames( 100 );
  std::vector<std::uint8_t> const state = machine.saveState();

  // What 30 more frames make of it, sound included.
  auto const runOn = [&machine]
  {
    std::vector<std::int16_t> sound;
    for ( int frame = 0; frame < 30; ++frame )
    {
      machine.runFrames( 1 );
      for ( auto const& f : machine.audio() )
      {
        sound.insert( sound.end(), { f.left, f.right } );
      }
    }
    auto const bytes = []( std::span<std::uint8_t const> span )
    { return std::vector<std::uint8_t>( span.begin(), span.end() ); };
    return std::tuple{ bytes( machine.workRam() ),
                       bytes( machine.z80Ram() ),
                       bytes( machine.videoRam() ),
                       bytes( machine.picture() ),
                       sound,
                       machine.now() };
  };
  auto const first = runOn();

  REQUIRE( machine.loadState( state ) );
  auto const second = runOn();

  REQUIRE( std::get<5>( first ) == std::get<5>( second ) );
  REQUIRE( std::get<0>( first ) == std::get<0>( second ) );
  REQUIRE( std::get<1>( first ) == std::get<1>( second ) );
  REQUIRE( std::get<2>( first ) == std::get<2>( second ) );
  REQUIRE( std::get<3>( first ) == std::get<3>( second ) );
  REQUIRE( std::get<4>( first ) == std::get<4>( second ) );
  REQUIRE_FALSE( std::get<4>( first ).empty() );

  // Something else is refused, and changes nothing.
  std::vector<std::uint8_t> broken = state;
  broken.resize( broken.size() / 2 );
  REQUIRE_FALSE( machine.loadState( broken ) );
  REQUIRE( machine.now() == std::get<5>( second ) );
}
