#include <catch2/catch_test_macros.hpp>

#include "pgm/Emulator.hpp"
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

TEST_CASE( "running ahead and coming back changes nothing, on every board", "[roms]" )
{
  // Run-ahead saves the state after each frame, runs on past it and loads it
  // again: a frame later the run must be the one it would have been. One game
  // a board, each run past frame 767, where killbld first asks its IGS025.
  auto const zipPath = romsDirectory() / "pgm.zip";
  if ( !std::filesystem::exists( zipPath ) || !std::filesystem::exists( pgmDirectory() / "orlegend.pgm" ) )
  {
    SKIP( "needs " << zipPath << " and the images in " << pgmDirectory() );
  }
  constexpr int warmUp = 800;
  constexpr int compared = 30;
  constexpr int ahead = 2;

  for ( char const* game : { "orlegend", "drgw2", "killbld", "kovsh", "ket", "kov2", "theglad", "olds103t" } )
  {
    CAPTURE( game );
    pgm::Emulator emulator{ pgm::Settings{
        .biosSources = { zipPath }, .romDirectory = pgmDirectory(), .stateDirectory = {} } };
    REQUIRE( emulator.loadGameByName( game ) );
    pgm::machine::Machine& machine = *emulator.machine();
    machine.runFrames( warmUp );
    std::vector<std::uint8_t> const start = machine.saveState();

    // Every frame's picture and sound, and the RAMs at the end.
    auto const run = [&machine]( bool runAhead )
    {
      std::vector<std::vector<std::uint8_t>> pictures;
      std::vector<std::int16_t> sound;
      for ( int frame = 0; frame < compared; ++frame )
      {
        machine.runFrames( 1 );
        pictures.emplace_back( machine.picture().begin(), machine.picture().end() );
        for ( auto const& f : machine.audio() )
        {
          sound.insert( sound.end(), { f.left, f.right } );
        }
        if ( runAhead )
        {
          std::vector<std::uint8_t> const state = machine.saveState();
          machine.runFrames( ahead );
          REQUIRE( machine.loadState( state ) );
        }
      }
      auto const bytes = []( std::span<std::uint8_t const> span )
      { return std::vector<std::uint8_t>( span.begin(), span.end() ); };
      return std::tuple{
        pictures,     sound, bytes( machine.workRam() ), bytes( machine.z80Ram() ), bytes( machine.videoRam() ),
        machine.now()
      };
    };
    auto const straight = run( false );
    REQUIRE( machine.loadState( start ) );
    auto const ahead2 = run( true );

    REQUIRE( std::get<5>( straight ) == std::get<5>( ahead2 ) );
    REQUIRE( std::get<0>( straight ) == std::get<0>( ahead2 ) );
    REQUIRE( std::get<1>( straight ) == std::get<1>( ahead2 ) );
    REQUIRE( std::get<2>( straight ) == std::get<2>( ahead2 ) );
    REQUIRE( std::get<3>( straight ) == std::get<3>( ahead2 ) );
    REQUIRE( std::get<4>( straight ) == std::get<4>( ahead2 ) );
  }
}
