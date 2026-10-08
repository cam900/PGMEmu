// Derived from MAME's code under the BSD-3-Clause licence: see Igs022.hpp.

#include "Igs022.hpp"

#include <bit>

namespace pgm::machine
{

namespace
{

// Shared RAM, by word index: the command, the completion code, and the
// commands' parameters at byte addresses 0x288 to 0x2A2.
constexpr std::uint32_t COMMAND = 0x100;
constexpr std::uint32_t STATUS = 0x101;
constexpr std::uint32_t PUSH_HIGH = 0x144;
constexpr std::uint32_t PUSH_LOW = 0x145;
constexpr std::uint32_t POP_HIGH = 0x146;
constexpr std::uint32_t POP_LOW = 0x147;
constexpr std::uint32_t DMA_SOURCE = 0x148;
constexpr std::uint32_t DMA_DESTINATION = 0x149;
constexpr std::uint32_t DMA_SIZE = 0x14a;
constexpr std::uint32_t DMA_MODE = 0x14b;
constexpr std::uint32_t OP_SOURCE1 = 0x14c;
constexpr std::uint32_t OP_SOURCE2 = 0x14d;
constexpr std::uint32_t OP_DESTINATION = 0x14e;
constexpr std::uint32_t OP_CODE = 0x14f;
constexpr std::uint32_t VERSION = 0x151;

// The ROM's words that describe the DMA run at reset, and the version word.
constexpr std::uint32_t RESET_SOURCE = 0x80;
constexpr std::uint32_t RESET_DESTINATION = 0x81;
constexpr std::uint32_t RESET_SIZE = 0x82;
constexpr std::uint32_t RESET_MODE = 0x83;
constexpr std::uint32_t RESET_VERSION = 0x8a;

/// Command 0x6D's destination that pushes the result instead of keeping it.
constexpr std::uint16_t TO_STACK = 0x300;

constexpr std::uint16_t A55A = 0xa55a;

std::uint16_t nibbleSwap( std::uint16_t value )
{
  return static_cast<std::uint16_t>( ( ( value & 0x0f0fU ) << 4U ) | ( ( value & 0xf0f0U ) >> 4U ) );
}

/// Mode 4's key, "IGS ", a character of it in each byte by the word's index.
constexpr std::array<std::uint8_t, 4> MODE4_KEY{ 'I', 'G', 'S', ' ' };

std::uint16_t mode4Key( std::uint16_t index )
{
  return static_cast<std::uint16_t>( ( MODE4_KEY.at( ( index >> 8U ) & 3U ) << 8U ) | MODE4_KEY.at( index & 3U ) );
}

} // namespace

Igs022::Igs022( std::span<std::uint8_t const> rom ) : mRom{ rom } {}

void Igs022::reset()
{
  mStackPtr = 0;
  mShared.fill( A55A );
  Transfer transfer{ .source = static_cast<std::uint16_t>( readRomWord( RESET_SOURCE ) >> 1U ),
                     .destination = readRomWord( RESET_DESTINATION ),
                     .size = readRomWord( RESET_SIZE ) };
  // The mode word is read with its bytes the other way round.
  std::uint16_t const mode = readRomWord( RESET_MODE );
  transfer.mode = static_cast<std::uint8_t>( ( mode >> 8U ) & 7U );
  transfer.param = static_cast<std::uint8_t>( mode );
  dma( transfer );
  writeShared( VERSION, readRomWord( RESET_VERSION ) );
}

std::int64_t Igs022::execute()
{
  // The trigger's tick, the command's fetch and its decode.
  mTicks = 2;
  std::uint16_t const command = readShared( COMMAND );
  ++mTicks;
  switch ( command )
  {
  case 0x12: // push
  {
    std::uint16_t const high = readShared( PUSH_HIGH );
    ++mTicks;
    std::uint16_t const low = readShared( PUSH_LOW );
    mTicks += 2;
    push( ( static_cast<std::uint32_t>( high ) << 16U ) | low );
    setStatus( 0x23 );
    break;
  }
  case 0x45: // pop
  {
    std::uint32_t const value = mStack.at( mStackPtr );
    if ( mStackPtr != 0 )
    {
      --mStackPtr;
    }
    mTicks += 2;
    writeShared( POP_HIGH, static_cast<std::uint16_t>( value >> 16U ) );
    ++mTicks;
    writeShared( POP_LOW, static_cast<std::uint16_t>( value ) );
    ++mTicks;
    setStatus( 0x56 );
    break;
  }
  case 0x4f: // DMA
  {
    Transfer transfer{ .source = static_cast<std::uint16_t>( readShared( DMA_SOURCE ) >> 1U ) };
    ++mTicks;
    transfer.destination = readShared( DMA_DESTINATION );
    ++mTicks;
    transfer.size = readShared( DMA_SIZE );
    ++mTicks;
    std::uint16_t const mode = readShared( DMA_MODE );
    ++mTicks;
    transfer.mode = static_cast<std::uint8_t>( mode & 7U );
    transfer.param = static_cast<std::uint8_t>( mode >> 8U );
    dma( transfer );
    setStatus( 0x5e );
    break;
  }
  case 0x2d:
    setStatus( 0x3c );
    break;
  case 0x5a:
    setStatus( 0x4b );
    break;
  case 0x6d: // arithmetic on the registers
  {
    std::uint16_t const source1 = readShared( OP_SOURCE1 );
    ++mTicks;
    std::uint16_t const source2 = readShared( OP_SOURCE2 );
    ++mTicks;
    std::uint16_t const destination = readShared( OP_DESTINATION );
    ++mTicks;
    std::uint16_t const op = readShared( OP_CODE );
    mTicks += 2;
    std::uint32_t const operand1 = source1 < REGS ? mRegs.at( source1 ) : 0;
    std::uint32_t const operand2 = source2 < REGS ? mRegs.at( source2 ) : 0;
    std::uint32_t result = 0;
    switch ( op )
    {
    case 0x0:
      result = operand1 + operand2;
      break;
    case 0x1:
      result = operand1 - operand2;
      break;
    case 0x6:
      result = operand2 - operand1;
      break;
    case 0x9:
      result = ( static_cast<std::uint32_t>( source1 ) << 16U ) | source2;
      break;
    case 0xa: // get
      ++mTicks;
      writeShared( OP_DESTINATION, static_cast<std::uint16_t>( operand1 >> 16U ) );
      ++mTicks;
      writeShared( OP_CODE, static_cast<std::uint16_t>( operand1 ) );
      setStatus( 0x7c );
      return mTicks;
    default:
      setStatus( 0x7c );
      return mTicks;
    }
    if ( destination == TO_STACK )
    {
      push( result );
    }
    else if ( destination < REGS )
    {
      mRegs.at( destination ) = result;
      ++mTicks;
    }
    setStatus( 0x7c );
    break;
  }
  default:
    // An unknown command goes back to idle without a completion code.
    break;
  }
  return mTicks;
}

std::uint16_t Igs022::read( std::uint32_t index ) const
{
  return mShared.at( index & ( SHARED_WORDS - 1 ) );
}

void Igs022::write( std::uint32_t index, std::uint16_t value, bool upper, bool lower )
{
  std::uint16_t& word = mShared.at( index & ( SHARED_WORDS - 1 ) );
  if ( upper )
  {
    word = static_cast<std::uint16_t>( ( word & 0x00ffU ) | ( value & 0xff00U ) );
  }
  if ( lower )
  {
    word = static_cast<std::uint16_t>( ( word & 0xff00U ) | ( value & 0x00ffU ) );
  }
}

std::uint16_t Igs022::readShared( std::uint32_t index )
{
  mTicks += 2;
  return mShared.at( index & ( SHARED_WORDS - 1 ) );
}

void Igs022::writeShared( std::uint32_t index, std::uint16_t value )
{
  ++mTicks;
  mShared.at( index & ( SHARED_WORDS - 1 ) ) = value;
}

std::uint16_t Igs022::readRomWord( std::uint32_t index )
{
  return readRomBytes( ( index & 0x7fffU ) << 1U );
}

std::uint16_t Igs022::readRomBytes( std::uint32_t address )
{
  std::uint8_t const low = romByte( address );
  std::uint8_t const high = romByte( ( address + 1 ) & 0xffffU );
  mCacheWord = 0;
  return static_cast<std::uint16_t>( ( high << 8U ) | low );
}

std::uint8_t Igs022::romByte( std::uint32_t address )
{
  std::uint32_t const word = address >> 2U;
  mTicks += word == mCacheWord ? 1 : 2;
  mCacheWord = word;
  return address < mRom.size() ? mRom[address] : 0;
}

void Igs022::push( std::uint32_t value )
{
  if ( mStackPtr != STACK - 1 )
  {
    ++mStackPtr;
  }
  mStack.at( mStackPtr ) = value;
  ++mTicks;
}

void Igs022::dma( Transfer const& transfer )
{
  for ( std::uint16_t x = 0;; ++x )
  {
    ++mTicks;
    if ( transfer.mode == 7 || x >= transfer.size )
    {
      return;
    }
    std::uint16_t data = readRomWord( static_cast<std::uint16_t>( transfer.source + x ) );
    ++mTicks;
    switch ( transfer.mode )
    {
    case 1:
    case 2:
    case 3:
    {
      std::uint16_t const key = readRomBytes( static_cast<std::uint8_t>( ( ( x & 0x7fU ) << 1U ) + transfer.param ) );
      ++mTicks;
      if ( transfer.mode == 1 )
      {
        data = static_cast<std::uint16_t>( data - key );
      }
      else if ( transfer.mode == 2 )
      {
        data = static_cast<std::uint16_t>( data + key );
      }
      else
      {
        data ^= key;
      }
      break;
    }
    case 4:
      data = static_cast<std::uint16_t>( data - mode4Key( x ) );
      break;
    case 5:
      data = std::rotl( data, 8 );
      break;
    case 6:
      data = nibbleSwap( data );
      break;
    default:
      break;
    }
    ++mTicks;
    writeShared( ( transfer.destination + x ) & ( SHARED_WORDS - 1 ), data );
    ++mTicks;
  }
}

void Igs022::setStatus( std::uint16_t status )
{
  ++mTicks;
  writeShared( STATUS, status );
}

} // namespace pgm::machine
