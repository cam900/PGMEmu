#include <catch2/catch_test_macros.hpp>

#include "machine/Igs027a.hpp"

#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>

using pgm::machine::Igs027a;
using pgm::machine::Igs027aBoard;
using pgm::machine::Time;
using pgm::machine::UNITS_PER_MASTER_TICK;

namespace
{

/// A 16 KB internal ROM holding ARM words from address 0, and others where
/// given.
std::vector<std::uint8_t> internalRom( std::initializer_list<std::pair<std::uint32_t, std::uint32_t>> words )
{
  std::vector<std::uint8_t> rom( 0x4000, 0 );
  for ( auto const& [address, word] : words )
  {
    for ( unsigned i = 0; i < 4; ++i )
    {
      rom.at( address + i ) = static_cast<std::uint8_t>( word >> ( i * 8 ) );
    }
  }
  return rom;
}

constexpr Igs027aBoard TYPE1{
  .type = Igs027aBoard::Type::TYPE1, .latch = 0x500000, .latchBytes = 4, .share = 0x4f0000, .shareBytes = 0x40, .fiq = 0
};

constexpr Igs027aBoard TYPE2{ .type = Igs027aBoard::Type::TYPE2,
                              .latch = 0xd10000,
                              .latchBytes = 2,
                              .share = 0xd00000,
                              .shareBytes = 0x10000,
                              .fiq = 0 };

constexpr Igs027aBoard TYPE3{ .type = Igs027aBoard::Type::TYPE3,
                              .latch = 0x5c0300,
                              .latchBytes = 2,
                              .share = 0x500000,
                              .shareBytes = 0x10000,
                              .fiq = 0x5c0000 };

constexpr Time ticks( std::int64_t count )
{
  return count * UNITS_PER_MASTER_TICK;
}

constexpr std::uint32_t LOOP = 0xeafffffe; // b .

} // namespace

TEST_CASE( "type 1's ARM answers through the latch, and shares words high half first", "[igs027a]" )
{
  Igs027a chip{ TYPE1,
                internalRom( { { 0x00, 0xe59f0018 }, // ldr r0, =0x40000000
                               { 0x04, 0xe5901000 }, // ldr r1, [r0]
                               { 0x08, 0xe2811001 }, // add r1, r1, #1
                               { 0x0c, 0xe5801000 }, // str r1, [r0]
                               { 0x10, 0xe59f200c }, // ldr r2, =0x50800000
                               { 0x14, 0xe5821000 }, // str r1, [r2]
                               { 0x18, LOOP },
                               { 0x20, 0x40000000 },
                               { 0x24, 0x50800000 } } ),
                {} };
  chip.reset( 0 );
  Time time = 0;
  chip.write( time, 0x500000, 0x0041, true, true );
  chip.write( time, 0x500002, 0x0000, true, true );
  time = ticks( 1000 );
  REQUIRE( chip.read( time, 0x500000, true, true ) == 0x0042 );
  REQUIRE( chip.read( time, 0x500002, true, true ) == 0x0000 );
  // The ARM's word 0 is the 68000's halfwords 0 (its high half) and 1.
  REQUIRE( chip.read( time, 0x4f0000, true, true ) == 0x0000 );
  REQUIRE( chip.read( time, 0x4f0002, true, true ) == 0x0042 );
  REQUIRE( chip.decodes( 0x4f003e ) );
  REQUIRE_FALSE( chip.decodes( 0x4f0040 ) );
}

TEST_CASE( "the IGS027A's ARM runs its board's clock's cycles", "[igs027a]" )
{
  Igs027a chip{ TYPE1, internalRom( { { 0x00, LOOP } } ), {} };
  chip.reset( ticks( 100 ) );
  chip.advanceTo( ticks( 100 + 5000 ) );
  // 20 MHz, 2 of every 5 master ticks; a branch takes 3 cycles at most.
  REQUIRE( chip.arm().cycles() >= 2000 );
  REQUIRE( chip.arm().cycles() < 2003 );
}

TEST_CASE( "type 2's latch, written by the 68000, raises FIQ until the ARM reads it", "[igs027a]" )
{
  Igs027a chip{ TYPE2,
                internalRom( { { 0x00, 0xea00000e }, // b 0x40
                               { 0x1c, 0xe59f8014 }, // fiq: ldr r8, =0x38000000
                               { 0x20, 0xe5989000 }, // ldr r9, [r8]
                               { 0x24, 0xe25ef004 }, // subs pc, lr, #4
                               { 0x38, 0x38000000 }, //
                               { 0x40, 0xe321f093 }, // msr cpsr_c, #0x93: FIQ enabled
                               { 0x44, LOOP } } ),
                {} };
  chip.reset( 0 );
  Time time = ticks( 1000 );
  chip.write( time, 0xd10000, 0x1234, true, true );
  REQUIRE( chip.arm().state().fiqLine );
  chip.advanceTo( ticks( 2000 ) );
  REQUIRE_FALSE( chip.arm().state().fiqLine );
  REQUIRE( chip.arm().state().fiq.at( 1 ) == 0x1234 ); // R9 of FIQ mode
  REQUIRE( ( chip.arm().state().cpsr & 0x1fU ) == 0x13 );
}

TEST_CASE( "type 3's ARM and 68000 see the two banks of shared RAM crosswise", "[igs027a]" )
{
  Igs027a chip{ TYPE3,
                internalRom( { { 0x00, 0xe59f0010 }, // ldr r0, =0x38000000
                               { 0x04, 0xe3a01055 }, // mov r1, #0x55
                               { 0x08, 0xe5801000 }, // str r1, [r0]
                               { 0x0c, LOOP },
                               { 0x18, 0x38000000 } } ),
                {} };
  chip.reset( 0 );
  Time time = 0;
  chip.write( time, 0x500000, 0x00aa, true, true ); // the 68000's bank, 0
  time = ticks( 1000 );
  // The ARM wrote 0x55 into its bank, 1, which the 68000 does not see.
  REQUIRE( chip.read( time, 0x500000, true, true ) == 0x00aa );
  REQUIRE( chip.peekArm( 0x38000000, 4 ) == 0x55 );
  // A write to 0x5C0000 raises FIQ.
  chip.write( time, 0x5c0000, 0, true, true );
  REQUIRE( chip.arm().state().fiqLine );
}
