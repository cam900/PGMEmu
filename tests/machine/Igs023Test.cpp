#include <catch2/catch_test_macros.hpp>

#include "machine/Igs023.hpp"

using pgm::machine::Igs023;
using pgm::machine::Time;
using pgm::machine::UNITS_PER_DOT;
using pgm::machine::UNITS_PER_LINE;
using pgm::machine::UNITS_PER_MASTER_TICK;

namespace
{

constexpr std::uint32_t FLAGS = 0xb0e000;
constexpr std::uint32_t LINE_COUNTER = 0xb07000;
constexpr std::uint16_t IRQ4_ENABLE = 1U << 2U;
constexpr std::uint16_t IRQ6_ENABLE = 1U << 3U;

/// A moment in the middle of dot `dot` of line `line` of the first frame,
/// clear of the master tick on which the raster's logic sees it change.
Time at( int line, int dot )
{
  return ( line * UNITS_PER_LINE ) + ( dot * UNITS_PER_DOT ) + ( 4 * UNITS_PER_MASTER_TICK );
}

std::uint16_t readWord( Igs023& video, Time now, std::uint32_t address )
{
  return video.read( now, address, true, true );
}

} // namespace

TEST_CASE( "the line counter is zeroed where vblank ends and counts hsyncs", "[machine][igs023]" )
{
  Igs023 video;

  REQUIRE( readWord( video, at( 40, 10 ), LINE_COUNTER ) == 0 );
  REQUIRE( readWord( video, at( 40, 100 ), LINE_COUNTER ) == 1 );
  REQUIRE( readWord( video, at( 140, 100 ), LINE_COUNTER ) == 101 );
}

TEST_CASE( "interrupt 6 is raised where vblank begins, until its enable is cleared", "[machine][igs023]" )
{
  Igs023 video;
  video.write( at( 100, 0 ), FLAGS, IRQ6_ENABLE, true, true );

  video.advanceTo( at( 263, 600 ) );
  REQUIRE_FALSE( video.irq6() );

  Time const nextFrame = at( 264, 10 );
  video.advanceTo( nextFrame );
  REQUIRE( video.irq6() );

  video.advanceTo( nextFrame + UNITS_PER_LINE );
  REQUIRE( video.irq6() );

  video.write( nextFrame + UNITS_PER_LINE, FLAGS, 0, true, true );
  REQUIRE_FALSE( video.irq6() );
}

TEST_CASE( "interrupt 4 is raised every 62 hsyncs, from power-up, whatever the frame", "[machine][igs023]" )
{
  Igs023 video;
  video.write( 0, FLAGS, IRQ4_ENABLE, true, true );

  // The 62nd hsync since power-up is on line 61.
  video.advanceTo( at( 61, 10 ) );
  REQUIRE_FALSE( video.irq4() );
  video.advanceTo( at( 61, 100 ) );
  REQUIRE( video.irq4() );

  video.write( at( 61, 100 ), FLAGS, 0, true, true );
  video.write( at( 61, 100 ), FLAGS, IRQ4_ENABLE, true, true );
  video.advanceTo( at( 123, 10 ) );
  REQUIRE_FALSE( video.irq4() );
  video.advanceTo( at( 123, 100 ) );
  REQUIRE( video.irq4() );
}

TEST_CASE( "VRAM is byte-wide, the upper byte of a word at the odd address, with the RTL's mirrors",
           "[machine][igs023]" )
{
  Igs023 video;

  video.write( 0, 0x900010, 0xabcd, true, true );
  REQUIRE( video.vram()[0x11] == 0xab );
  REQUIRE( video.vram()[0x10] == 0xcd );

  // The background map folds into its first 4 KB; 0x6000 folds onto 0x4000.
  REQUIRE( readWord( video, 0, 0x901010 ) == 0xabcd );
  video.write( 0, 0x906020, 0x1234, true, true );
  REQUIRE( readWord( video, 0, 0x904020 ) == 0x1234 );

  // A byte write touches its byte alone.
  video.write( 0, 0x900010, 0x7777, false, true );
  REQUIRE( readWord( video, 0, 0x900010 ) == 0xab77 );
}

TEST_CASE( "palette RAM holds 4K words in the 68000's byte order, mirrored", "[machine][igs023]" )
{
  Igs023 video;

  video.write( 0, 0xa00002, 0x7fff, true, true );

  REQUIRE( video.palette()[2] == 0x7f );
  REQUIRE( video.palette()[3] == 0xff );
  REQUIRE( readWord( video, 0, 0xa02002 ) == 0x7fff );
}
