#include <catch2/catch_test_macros.hpp>

#include "machine/Igs022.hpp"
#include "machine/Igs022Igs025Board.hpp"
#include "machine/Igs025.hpp"

#include <cstdint>
#include <vector>

using pgm::machine::Igs022;
using pgm::machine::Igs022Igs025Board;
using pgm::machine::Igs025;

namespace
{

// IGS022 shared RAM, by word index.
constexpr std::uint32_t COMMAND = 0x100;
constexpr std::uint32_t STATUS = 0x101;

pgm::cart::Igs025Table killbldLike()
{
  pgm::cart::Igs025Table table{ .region = 0x21, .gameId = 0x89911421, .data = {} };
  for ( std::size_t i = 0; i < table.data.size(); ++i )
  {
    table.data.at( i ) = static_cast<std::uint8_t>( 0x40 + i );
  }
  return table;
}

/// A 64 KB IGS022 ROM, zero but for the words given, each stored low byte first.
std::vector<std::uint8_t> romWith( std::vector<std::pair<std::uint32_t, std::uint16_t>> const& words )
{
  std::vector<std::uint8_t> rom( 0x10000, 0 );
  for ( auto const& [index, value] : words )
  {
    std::size_t const at = std::size_t{ index } * 2;
    rom.at( at ) = static_cast<std::uint8_t>( value );
    rom.at( at + 1 ) = static_cast<std::uint8_t>( value >> 8U );
  }
  return rom;
}

std::int64_t run( Igs022& chip, std::uint16_t command )
{
  chip.write( COMMAND, command, true, true );
  return chip.execute();
}

} // namespace

TEST_CASE( "IGS025 reads the game id a byte a step, low byte first", "[igs025]" )
{
  Igs025 chip{ killbldLike() };
  chip.reset();
  chip.write( 0, 0x05 );
  REQUIRE( chip.read( 1 ) == 0x3f00 );
  std::vector<std::uint16_t> id;
  for ( std::uint16_t command = 0x20; command < 0x24; ++command )
  {
    chip.write( 0, command );
    chip.write( 1, 0 );
    chip.write( 0, 0x05 );
    id.push_back( chip.read( 1 ) );
  }
  REQUIRE( id == std::vector<std::uint16_t>{ 0x3f21, 0x3f14, 0x3f91, 0x3f89 } );
}

TEST_CASE( "IGS025's check value folds in what is written and the table read", "[igs025]" )
{
  Igs025 chip{ killbldLike() };
  chip.reset();
  // Two reads of the table: its second byte into the high half, its third
  // into the low.
  chip.write( 0, 0x40 );
  static_cast<void>( chip.read( 1 ) );
  static_cast<void>( chip.read( 1 ) );
  // Five steps, each with the bit the command picks set in its data.
  for ( auto const [command, data] : { std::pair{ 0x20, 0x01 },
                                       std::pair{ 0x21, 0x02 },
                                       std::pair{ 0x22, 0x04 },
                                       std::pair{ 0x23, 0x08 },
                                       std::pair{ 0x27, 0x80 } } )
  {
    chip.write( 0, static_cast<std::uint16_t>( command ) );
    chip.write( 1, static_cast<std::uint16_t>( data ) );
  }
  chip.write( 0, 0x05 );
  // Worked independently from MAME's killbld_protection_calculate_hold.
  REQUIRE( chip.peek( 1 ) == 0x3fcf );
}

TEST_CASE( "IGS025 starts an IGS022 command on either game's start word", "[igs025]" )
{
  Igs025 chip{ killbldLike() };
  chip.reset();

  chip.write( 0, 0x00 );
  chip.write( 1, 0x0005 );
  chip.write( 0, 0x01 );
  REQUIRE( chip.read( 1 ) == 0x0005 );
  REQUIRE( chip.write( 1, 0x0002 ) );
  REQUIRE_FALSE( chip.write( 1, 0x0001 ) );

  chip.write( 0, 0x02 );
  REQUIRE_FALSE( chip.write( 1, 0x0002 ) );
  REQUIRE( chip.write( 1, 0x0001 ) );
  chip.write( 0, 0x01 );
  REQUIRE( chip.read( 1 ) == 0x0006 ); // The Killing Blade's start counts.

  chip.write( 0, 0x03 );
  chip.write( 1, 0x0012 );
  chip.write( 0, 0x00 );
  REQUIRE( chip.read( 1 ) == 0x00c8 ); // 0x13, its bits reversed
  REQUIRE( chip.read( 0 ) == 0 );
}

TEST_CASE( "IGS022's reset fills its RAM and runs the DMA its ROM describes", "[igs022]" )
{
  auto const rom = romWith( { { 0x80, 0x0200 }, // from ROM word 0x100
                              { 0x81, 0x0010 }, // to RAM word 0x10
                              { 0x82, 0x0003 }, // three words
                              { 0x83, 0x0500 }, // mode 5: bytes swapped
                              { 0x8a, 0x1234 }, // the version word
                              { 0x100, 0x1122 },
                              { 0x101, 0x3344 },
                              { 0x102, 0x5566 } } );
  Igs022 chip{ rom };
  chip.reset();
  REQUIRE( chip.read( 0x0f ) == 0xa55a );
  REQUIRE( chip.read( 0x10 ) == 0x2211 );
  REQUIRE( chip.read( 0x11 ) == 0x4433 );
  REQUIRE( chip.read( 0x12 ) == 0x6655 );
  REQUIRE( chip.read( 0x13 ) == 0xa55a );
  REQUIRE( chip.read( 0x151 ) == 0x1234 );
}

TEST_CASE( "IGS022's DMA decrypts each word by its mode", "[igs022]" )
{
  // A data word at ROM word 0x100, and key bytes 0x01 0x02 at 0x10.
  auto rom = romWith( { { 0x100, 0x1234 }, { 0x08, 0x0201 } } );
  Igs022 chip{ rom };
  chip.reset();

  auto const dmaWith = [&]( std::uint16_t mode )
  {
    chip.write( 0x148, 0x0200, true, true ); // from ROM byte 0x200
    chip.write( 0x149, 0x0400, true, true ); // to RAM word 0x400
    chip.write( 0x14a, 1, true, true );
    chip.write( 0x14b, static_cast<std::uint16_t>( 0x1000 | mode ), true, true ); // param 0x10
    run( chip, 0x4f );
    REQUIRE( chip.read( STATUS ) == 0x5e );
    return chip.read( 0x400 );
  };
  REQUIRE( dmaWith( 0 ) == 0x1234 );
  REQUIRE( dmaWith( 1 ) == 0x1033 );
  REQUIRE( dmaWith( 2 ) == 0x1435 );
  REQUIRE( dmaWith( 3 ) == 0x1035 );
  REQUIRE( dmaWith( 4 ) == 0xc8eb ); // less "II"
  REQUIRE( dmaWith( 5 ) == 0x3412 );
  REQUIRE( dmaWith( 6 ) == 0x2143 );
}

TEST_CASE( "IGS022 pushes, pops and works on its registers", "[igs022]" )
{
  std::vector<std::uint8_t> const rom( 0x10000, 0 );
  Igs022 chip{ rom };
  chip.reset();

  for ( std::uint32_t const value : { 0x11112222U, 0x33334444U } )
  {
    chip.write( 0x144, static_cast<std::uint16_t>( value >> 16U ), true, true );
    chip.write( 0x145, static_cast<std::uint16_t>( value ), true, true );
    run( chip, 0x12 );
    REQUIRE( chip.read( STATUS ) == 0x23 );
  }
  run( chip, 0x45 );
  REQUIRE( chip.read( STATUS ) == 0x56 );
  REQUIRE( chip.read( 0x146 ) == 0x3333 );
  REQUIRE( chip.read( 0x147 ) == 0x4444 );
  run( chip, 0x45 );
  REQUIRE( chip.read( 0x146 ) == 0x1111 );

  auto const op = [&]( std::uint16_t code, std::uint16_t source1, std::uint16_t source2, std::uint16_t destination )
  {
    chip.write( 0x14c, source1, true, true );
    chip.write( 0x14d, source2, true, true );
    chip.write( 0x14e, destination, true, true );
    chip.write( 0x14f, code, true, true );
    run( chip, 0x6d );
    REQUIRE( chip.read( STATUS ) == 0x7c );
  };
  op( 0x9, 0x0001, 0x0002, 5 ); // regs[5] = 0x00010002
  op( 0x0, 5, 5, 6 );           // regs[6] = regs[5] + regs[5]
  op( 0x6, 6, 5, 7 );           // regs[7] = regs[5] - regs[6]
  op( 0xa, 6, 0, 0 );           // get regs[6]
  REQUIRE( chip.read( 0x14e ) == 0x0002 );
  REQUIRE( chip.read( 0x14f ) == 0x0004 );
  op( 0xa, 7, 0, 0 );
  REQUIRE( chip.read( 0x14e ) == 0xfffe );
  REQUIRE( chip.read( 0x14f ) == 0xfffe );
}

TEST_CASE( "IGS022 is busy as many master ticks as the RTL's engine", "[igs022]" )
{
  std::vector<std::uint8_t> const rom( 0x10000, 0 );
  Igs022 chip{ rom };
  chip.reset();
  // The trigger, the fetch (two), the decode, the status and its write.
  REQUIRE( run( chip, 0x2d ) == 7 );
  REQUIRE( chip.read( STATUS ) == 0x3c );
  // An unknown command goes back to idle after its decode, writing nothing.
  chip.write( STATUS, 0, true, true );
  REQUIRE( run( chip, 0x77 ) == 5 );
  REQUIRE( chip.read( STATUS ) == 0 );
}

TEST_CASE( "The Killing Blade's board holds the 68000 while the IGS022 works", "[igs025]" )
{
  std::vector<std::uint8_t> const rom( 0x10000, 0 );
  Igs022Igs025Board board{ rom, killbldLike(), 0xd40000 };
  board.reset( 0 );
  REQUIRE( board.decodes( 0x300000 ) );
  REQUIRE( board.decodes( 0x303ffe ) );
  REQUIRE_FALSE( board.decodes( 0x304000 ) );
  REQUIRE( board.decodes( 0xd40002 ) );
  REQUIRE_FALSE( board.decodes( 0xd40004 ) );

  pgm::machine::Time time = 0;
  board.write( time, 0x300200, 0x002d, true, true );
  board.write( time, 0xd40000, 0x0002, true, true );
  REQUIRE( time == 0 );
  board.write( time, 0xd40002, 0x0001, true, true );
  // Seven master ticks, fourteen units, rounded up to three 68000 cycles.
  REQUIRE( time == 15 );
  REQUIRE( board.read( time, 0x300202, true, true ) == 0x3c );
}
