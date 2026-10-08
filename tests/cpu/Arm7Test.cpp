#include <catch2/catch_test_macros.hpp>

#include "cpu/Arm7.hpp"
#include "cpu/Arm7Disassembler.hpp"

#include <array>
#include <cstdint>
#include <initializer_list>

using pgm::cpu::Arm7;
using pgm::cpu::Arm7Bus;

namespace
{

/// 64 KB of little-endian RAM.
class Ram final : public Arm7Bus
{
public:
  std::array<std::uint8_t, 0x10000> bytes{};

  void put( std::uint32_t address, std::initializer_list<std::uint32_t> words )
  {
    for ( std::uint32_t const word : words )
    {
      for ( unsigned i = 0; i < 4; ++i )
      {
        bytes.at( address + i ) = static_cast<std::uint8_t>( word >> ( i * 8 ) );
      }
      address += 4;
    }
  }

  void putHalfwords( std::uint32_t address, std::initializer_list<std::uint16_t> halfwords )
  {
    for ( std::uint16_t const halfword : halfwords )
    {
      bytes.at( address ) = static_cast<std::uint8_t>( halfword );
      bytes.at( address + 1 ) = static_cast<std::uint8_t>( halfword >> 8U );
      address += 2;
    }
  }

  std::uint32_t read( std::uint32_t address, unsigned size, unsigned /*access*/ ) override
  {
    address &= 0xffffU & ~( size - 1 );
    std::uint32_t value = 0;
    for ( unsigned i = 0; i < size; ++i )
    {
      value |= std::uint32_t{ bytes.at( address + i ) } << ( i * 8 );
    }
    return value;
  }

  void write( std::uint32_t address, unsigned size, std::uint32_t value, unsigned /*access*/ ) override
  {
    address &= 0xffffU & ~( size - 1 );
    for ( unsigned i = 0; i < size; ++i )
    {
      bytes.at( address + i ) = static_cast<std::uint8_t>( value >> ( i * 8 ) );
    }
  }
};

constexpr std::uint32_t B_MAIN = 0xea00003e; // b 0x100, from 0

} // namespace

TEST_CASE( "the ARM7TDMI resets into supervisor mode at address 0", "[arm7]" )
{
  Ram ram;
  ram.put( 0, { 0xe3a00005 } ); // mov r0, #5
  Arm7 cpu{ ram };
  cpu.reset();
  REQUIRE( cpu.pc() == 0 );
  REQUIRE( ( cpu.state().cpsr & Arm7::MODE_MASK ) == Arm7::MODE_SUPERVISOR );
  REQUIRE( ( cpu.state().cpsr & ( Arm7::DISABLE_IRQ | Arm7::DISABLE_FIQ ) ) ==
           ( Arm7::DISABLE_IRQ | Arm7::DISABLE_FIQ ) );
  cpu.step();
  REQUIRE( cpu.reg( 0 ) == 5 );
}

TEST_CASE( "the ARM7TDMI takes the cycles its manual gives", "[arm7]" )
{
  Ram ram;
  ram.put( 0,
           { 0xe3a00005,     // mov r0, #5
             0xe2500001,     // loop: subs r0, r0, #1
             0x1afffffd,     // bne loop
             0xeafffffe } ); // b .
  Arm7 cpu{ ram };
  cpu.reset();
  while ( cpu.pc() != 0xc )
  {
    cpu.step();
  }
  // The reset's two fetches; MOV, 1S; four turns of SUBS, 1S, and a taken
  // BNE, 2S+1N; the last SUBS, and the BNE not taken, 1S each.
  REQUIRE( cpu.cycles() == 2 + 1 + ( 4 * ( 1 + 3 ) ) + 1 + 1 );
  REQUIRE( cpu.reg( 0 ) == 0 );
}

TEST_CASE( "the ARM7TDMI takes a FIQ between instructions and returns from it", "[arm7]" )
{
  Ram ram;
  ram.put( 0, { B_MAIN } );
  ram.put( 0x1c, { 0xe25ef004 } ); // subs pc, lr, #4
  ram.put( 0x100,
           { 0xe321f01f,     // msr cpsr_c, #0x1f: system mode, interrupts enabled
             0xe3a01001,     // mov r1, #1
             0xe3a01002,     // mov r1, #2
             0xeafffffe } ); // b .
  Arm7 cpu{ ram };
  cpu.reset();
  cpu.step(); // b main
  cpu.step(); // msr
  cpu.step(); // mov r1, #1
  REQUIRE( cpu.reg( 1 ) == 1 );

  cpu.setFiq( true );
  cpu.step();
  REQUIRE( cpu.pc() == 0x1c );
  REQUIRE( ( cpu.state().cpsr & Arm7::MODE_MASK ) == Arm7::MODE_FIQ );
  REQUIRE( ( cpu.state().cpsr & Arm7::DISABLE_FIQ ) != 0 );
  REQUIRE( cpu.reg( 14 ) == 0x10c ); // the next instruction, 0x108, and 4
  REQUIRE( cpu.state().spsr[0] == 0x1f );

  cpu.setFiq( false );
  cpu.step(); // subs pc, lr, #4
  REQUIRE( cpu.pc() == 0x108 );
  REQUIRE( cpu.state().cpsr == 0x1f );
  cpu.step();
  REQUIRE( cpu.reg( 1 ) == 2 );
}

TEST_CASE( "the ARM7TDMI's FIQ is held off while masked", "[arm7]" )
{
  Ram ram;
  ram.put( 0, { 0xe3a01001, 0xe3a01002, 0xeafffffe } ); // mov r1, #1; mov r1, #2; b .
  Arm7 cpu{ ram };
  cpu.reset(); // FIQ disabled
  cpu.setFiq( true );
  cpu.step();
  cpu.step();
  REQUIRE( cpu.reg( 1 ) == 2 );
  REQUIRE( ( cpu.state().cpsr & Arm7::MODE_MASK ) == Arm7::MODE_SUPERVISOR );
}

TEST_CASE( "the ARM7TDMI enters Thumb state through BX and runs there", "[arm7]" )
{
  Ram ram;
  ram.put( 0, { B_MAIN } );
  ram.put( 0x100,
           { 0xe28f0001,     // add r0, pc, #1: 0x109
             0xe12fff10 } ); // bx r0
  ram.putHalfwords( 0x108,
                    { 0x2107,     // movs r1, #7
                      0x3101,     // adds r1, #1
                      0xe7fe } ); // b .
  Arm7 cpu{ ram };
  cpu.reset();
  for ( int i = 0; i < 5; ++i )
  {
    cpu.step();
  }
  REQUIRE( ( cpu.state().cpsr & Arm7::THUMB ) != 0 );
  REQUIRE( cpu.reg( 1 ) == 8 );
  REQUIRE( cpu.pc() == 0x10c );
}

TEST_CASE( "ARM and Thumb instructions disassemble in ARM's syntax", "[arm7]" )
{
  using pgm::cpu::disassembleArm;
  using pgm::cpu::disassembleThumb;
  REQUIRE( disassembleArm( 0, 0xe3a00005 ) == "mov r0, #0x5" );
  REQUIRE( disassembleArm( 4, 0xe2500001 ) == "subs r0, r0, #0x1" );
  REQUIRE( disassembleArm( 8, 0x1afffffd ) == "bne 0x00000004" );
  REQUIRE( disassembleArm( 0, 0xe25ef004 ) == "subs pc, lr, #0x4" );
  REQUIRE( disassembleArm( 0, 0xe12fff10 ) == "bx r0" );
  REQUIRE( disassembleArm( 0, 0xe92d4ff0 ) == "stmdb sp!, {r4-r11,lr}" );
  REQUIRE( disassembleArm( 0, 0xe5912004 ) == "ldr r2, [r1, #0x4]" );
  REQUIRE( disassembleArm( 0, 0xe0810392 ) == "umull r0, r1, r2, r3" );
  REQUIRE( disassembleArm( 0, 0xe10f0000 ) == "mrs r0, cpsr" );
  REQUIRE( disassembleArm( 0, 0xe321f01f ) == "msr cpsr_c, #0x1f" );
  REQUIRE( disassembleArm( 0, 0xe1d320b2 ) == "ldrh r2, [r3, #0x2]" );
  REQUIRE( disassembleArm( 0, 0xe1a01102 ) == "mov r1, r2, lsl #2" );
  REQUIRE( disassembleThumb( 0x108, 0x2107, 0 ) == "mov r1, #0x7" );
  REQUIRE( disassembleThumb( 0x10c, 0xe7fe, 0 ) == "b 0x0000010c" );
  REQUIRE( disassembleThumb( 0, 0xb5f0, 0 ) == "push {r4-r7,lr}" );
  REQUIRE( disassembleThumb( 0x200, 0xf000, 0xf804 ) == "bl 0x0000020c" );
  REQUIRE( disassembleThumb( 0, 0x4770, 0 ) == "bx lr" );
}
