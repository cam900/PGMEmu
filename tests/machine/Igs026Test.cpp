#include <catch2/catch_test_macros.hpp>

#include "machine/Ics2115.hpp"
#include "machine/Igs026.hpp"
#include "machine/Z80.hpp"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <vector>

using pgm::machine::Ics2115;
using pgm::machine::Igs026;
using pgm::machine::Sdram;
using pgm::machine::Time;
using pgm::machine::Z80;

namespace
{

constexpr std::uint32_t LATCH_1 = 0xc00002;
constexpr std::uint32_t Z80_CONTROL = 0xc00008;
constexpr std::uint32_t Z80_BUS = 0xc0000a;
constexpr std::uint32_t LATCH_6 = 0xc0000c;
constexpr std::uint32_t Z80_RAM = 0xc10000;

constexpr std::uint16_t BUS_REQUEST = 0x45d3;
/// What the BIOS writes to 0xC00008 to hold the Z80 in reset, and to run it.
constexpr std::uint16_t HOLD_Z80 = 0xa659;
constexpr std::uint16_t RUN_Z80 = 0x5050;

/// A millisecond: some 8500 of the Z80's T-states.
constexpr Time MILLISECOND = 100'000;

/// The sound side of the board, as the 68000 reaches it.
struct Board
{
  Sdram sdram;
  Ics2115 ics2115{ sdram, {} };
  Z80 z80;
  Igs026 io{ z80, ics2115 };

  /// Holds the Z80 in reset, writes `program` into its RAM from address 0,
  /// and lets it run, as the BIOS loads a sound driver.
  void load( Time now, std::initializer_list<std::uint8_t> program )
  {
    io.write( now, Z80_CONTROL, HOLD_Z80, true, true );
    io.write( now, Z80_BUS, BUS_REQUEST, true, true );
    std::uint32_t address = 0;
    for ( std::uint8_t const byte : program )
    {
      bool const odd = ( address & 1U ) != 0;
      io.write( now, Z80_RAM + ( address & ~1U ), odd ? byte : static_cast<std::uint16_t>( byte << 8U ), !odd, odd );
      ++address;
    }
    io.write( now, Z80_BUS, 0, true, true );
    io.write( now, Z80_CONTROL, RUN_Z80, true, true );
  }
};

} // namespace

TEST_CASE( "a register takes a write only on its lower strobe, and then the whole bus", "[machine][igs026]" )
{
  Board board;

  board.io.write( 0, LATCH_1, 0x1212, true, false );
  REQUIRE( board.io.read( 0, LATCH_1, true, true ) == 0 );

  board.io.write( 0, LATCH_1, 0x3434, false, true );
  REQUIRE( board.io.read( 0, LATCH_1, true, true ) == 0x3434 );
}

TEST_CASE( "the Z80's RAM is the 68000's only while it holds the Z80's bus", "[machine][igs026]" )
{
  Board board;
  Igs026& io = board.io;

  SECTION( "without asking for it, the 68000 reads nothing and writes nothing" )
  {
    io.write( 0, Z80_RAM + 4, 0xbeef, true, true );
    REQUIRE( io.z80Ram()[4] == 0 );
    REQUIRE( io.read( 0, Z80_RAM + 4, true, true ) == 0 );
  }

  SECTION( "a running Z80 grants it between two instructions" )
  {
    io.write( 0, Z80_BUS, BUS_REQUEST, true, true );
    io.write( 0, Z80_RAM + 4, 0xbeef, true, true );
    REQUIRE( io.z80Ram()[4] == 0 );

    io.write( MILLISECOND, Z80_RAM + 4, 0xbeef, true, true );
    REQUIRE( io.z80Ram()[4] == 0xbe );
    REQUIRE( io.z80Ram()[5] == 0xef );
    REQUIRE( io.read( MILLISECOND, Z80_RAM + 4, true, true ) == 0xbeef );

    io.write( MILLISECOND, Z80_BUS, 0x0a0a, true, true );
    REQUIRE( io.read( MILLISECOND, Z80_RAM + 4, true, true ) == 0 );
  }

  SECTION( "a Z80 held in reset grants it at once" )
  {
    io.write( 0, Z80_CONTROL, HOLD_Z80, true, true );
    io.write( 0, Z80_BUS, BUS_REQUEST, true, true );
    io.write( 0, Z80_RAM + 4, 0xbeef, true, true );
    REQUIRE( io.z80Ram()[4] == 0xbe );
  }
}

TEST_CASE( "the Z80 runs what the 68000 loads, and writes a latch through its I/O space", "[machine][igs026]" )
{
  Board board;
  board.load( 0,
              {
                  0x01,
                  0x00,
                  0x01, // ld bc,0x0100: latch 6
                  0x3e,
                  0x5a, // ld a,0x5a
                  0xed,
                  0x79, // out (c),a
                  0x76, // halt
              } );

  REQUIRE( board.io.read( MILLISECOND, LATCH_6, true, true ) == 0x005a );
  REQUIRE( board.z80.halted() );
}

TEST_CASE( "a command in latch 1 interrupts the Z80, and reading it clears the NMI", "[machine][igs026]" )
{
  Board board;
  std::initializer_list<std::uint8_t> const main = {
    0x31, 0x00, 0x80, // ld sp,0x8000
    0x18, 0xfe,       // jr $
  };
  std::initializer_list<std::uint8_t> const nmi = {
    0x01, 0x00, 0x02, // ld bc,0x0200: latch 1
    0xed, 0x78,       // in a,(c)
    0x3c,             // inc a
    0x06, 0x01,       // ld b,0x01: latch 6
    0xed, 0x79,       // out (c),a
    0xed, 0x45,       // retn
  };
  std::vector<std::uint8_t> program( 0x66 + nmi.size() );
  std::ranges::copy( main, program.begin() );
  std::ranges::copy( nmi, program.begin() + 0x66 );
  Igs026& io = board.io;
  io.write( 0, Z80_CONTROL, HOLD_Z80, true, true );
  io.write( 0, Z80_BUS, BUS_REQUEST, true, true );
  for ( std::uint32_t address = 0; address < program.size(); address += 2 )
  {
    std::uint8_t const next = address + 1 < program.size() ? program[address + 1] : 0;
    io.write( 0, Z80_RAM + address, static_cast<std::uint16_t>( ( program[address] << 8U ) | next ), true, true );
  }
  io.write( 0, Z80_BUS, 0, true, true );
  io.write( 0, Z80_CONTROL, RUN_Z80, true, true );

  io.write( MILLISECOND, LATCH_1, 0x0010, false, true );
  REQUIRE( io.read( 2 * MILLISECOND, LATCH_6, true, true ) == 0x0011 );

  // A second command is a second edge.
  io.write( 2 * MILLISECOND, LATCH_1, 0x0020, false, true );
  REQUIRE( io.read( 3 * MILLISECOND, LATCH_6, true, true ) == 0x0021 );
}

TEST_CASE( "the Z80 reaches the ICS2115 at I/O addresses 0x0000 to 0x0003", "[machine][igs026]" )
{
  Board board;
  board.load( 0,
              {
                  0x01,
                  0x01,
                  0x00, // ld bc,0x0001: register select
                  0x3e,
                  0x4c, // ld a,0x4c: chip revision
                  0xed,
                  0x79, // out (c),a
                  0x0e,
                  0x03, // ld c,0x03: high byte
                  0xed,
                  0x78, // in a,(c)
                  0x01,
                  0x00,
                  0x01, // ld bc,0x0100: latch 6
                  0xed,
                  0x79, // out (c),a
                  0x76, // halt
              } );

  REQUIRE( board.io.read( MILLISECOND, LATCH_6, true, true ) == 0x0001 );
}

TEST_CASE( "reset clears the latches and keeps the Z80's RAM", "[machine][igs026]" )
{
  Board board;
  Igs026& io = board.io;
  io.write( 0, Z80_CONTROL, HOLD_Z80, true, true );
  io.write( 0, Z80_BUS, BUS_REQUEST, true, true );
  io.write( 0, Z80_RAM, 0x0102, true, true );

  io.reset( 0 );

  REQUIRE( io.read( 0, Z80_BUS, true, true ) == 0 );
  REQUIRE( io.read( 0, Z80_CONTROL, true, true ) == 0 );
  REQUIRE( io.z80Ram()[0] == 0x01 );
}
