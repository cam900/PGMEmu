#include <catch2/catch_test_macros.hpp>

#include "machine/Igs026.hpp"

using pgm::machine::Igs026;

namespace
{

constexpr std::uint32_t LATCH_1 = 0xc00002;
constexpr std::uint32_t Z80_BUS = 0xc0000a;
constexpr std::uint32_t Z80_RAM = 0xc10000;

} // namespace

TEST_CASE( "a register takes a write only on its lower strobe, and then the whole bus", "[machine][igs026]" )
{
  Igs026 io;

  io.write( 0, LATCH_1, 0x1212, true, false );
  REQUIRE( io.read( 0, LATCH_1, true, true ) == 0 );

  io.write( 0, LATCH_1, 0x3434, false, true );
  REQUIRE( io.read( 0, LATCH_1, true, true ) == 0x3434 );
}

TEST_CASE( "the Z80's RAM is the 68000's only while it holds the Z80's bus", "[machine][igs026]" )
{
  Igs026 io;

  io.write( 0, Z80_RAM + 4, 0xbeef, true, true );
  REQUIRE( io.z80Ram()[4] == 0 );
  REQUIRE( io.read( 0, Z80_RAM + 4, true, true ) == 0 );

  io.write( 0, Z80_BUS, 0x45d3, true, true );
  io.write( 0, Z80_RAM + 4, 0xbeef, true, true );
  REQUIRE( io.z80Ram()[4] == 0xbe );
  REQUIRE( io.z80Ram()[5] == 0xef );
  REQUIRE( io.read( 0, Z80_RAM + 4, true, true ) == 0xbeef );

  io.write( 0, Z80_BUS, 0x0a0a, true, true );
  REQUIRE( io.read( 0, Z80_RAM + 4, true, true ) == 0 );
}

TEST_CASE( "reset clears the latches and keeps the Z80's RAM", "[machine][igs026]" )
{
  Igs026 io;
  io.write( 0, Z80_BUS, 0x45d3, true, true );
  io.write( 0, Z80_RAM, 0x0102, true, true );

  io.reset();

  REQUIRE( io.read( 0, Z80_BUS, true, true ) == 0 );
  REQUIRE( io.z80Ram()[0] == 0x01 );
}
