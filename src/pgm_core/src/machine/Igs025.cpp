#include "Igs025.hpp"

#include <bit>

namespace pgm::machine
{

namespace
{

std::uint32_t bit( std::uint32_t value, unsigned index )
{
  return ( value >> index ) & 1U;
}

/// killbld_protection_calculate_hold, as igs025.sv's calc_hold has it.
std::uint16_t nextHold( std::uint16_t hold, std::uint16_t hilo, unsigned y, std::uint8_t z )
{
  std::uint32_t h = std::rotl( hold, 1 );
  h ^= 0x2badU;
  h ^= bit( z, y & 7U );
  h ^= bit( hold, 7 );
  h ^= ( bit( hold, 13 ) ^ 1U ) << 4U;
  h ^= bit( hold, 3 ) << 11U;
  h ^= ( hilo & 0xfbf7U ) << 1U;
  return static_cast<std::uint16_t>( h );
}

/// The byte with its bits in the opposite order.
std::uint8_t reversed( std::uint8_t value )
{
  std::uint32_t result = 0;
  for ( unsigned i = 0; i < 8; ++i )
  {
    result = ( result << 1U ) | bit( value, i );
  }
  return static_cast<std::uint8_t>( result );
}

} // namespace

Igs025::Igs025( cart::Igs025Table const& table ) : mTable{ table } {}

void Igs025::reset()
{
  mCommand = 0;
  mReg = 0;
  mPtr = 0;
  mSwap = 0;
  mHold = 0;
  mHilo = 0;
  mHiloSelect = 0;
}

std::uint16_t Igs025::read( unsigned offset )
{
  std::uint16_t const value = peek( offset );
  if ( offset == 1 && ( mCommand & 0xffU ) == 0x40 )
  {
    // The table is read a byte at a time into the high and low halves in turn.
    mHiloSelect = mHiloSelect >= mTable.data.size() - 1 ? 0 : static_cast<std::uint8_t>( mHiloSelect + 1 );
    std::uint32_t const source = mTable.data.at( mHiloSelect );
    mHilo = ( mHiloSelect & 1U ) != 0 ? static_cast<std::uint16_t>( ( source << 8U ) | ( mHilo & 0x00ffU ) )
                                      : static_cast<std::uint16_t>( ( mHilo & 0xff00U ) | source );
  }
  return value;
}

std::uint16_t Igs025::peek( unsigned offset ) const
{
  if ( offset == 0 )
  {
    return 0;
  }
  switch ( mCommand & 0xffU )
  {
  case 0x00:
    return reversed( static_cast<std::uint8_t>( ( mSwap + 1 ) & 0x7fU ) );
  case 0x01:
    return mReg & 0x7fU;
  case 0x05:
    if ( mPtr >= 1 && mPtr <= 4 )
    {
      // The game id, its low byte first.
      return static_cast<std::uint16_t>( 0x3f00U | ( ( mTable.gameId >> ( ( mPtr - 1U ) * 8U ) ) & 0xffU ) );
    }
    return static_cast<std::uint16_t>( 0x3f00U | ( bit( mHold, 5 ) << 7U ) | ( bit( mHold, 2 ) << 6U ) |
                                       ( bit( mHold, 9 ) << 5U ) | ( bit( mHold, 7 ) << 4U ) |
                                       ( bit( mHold, 10 ) << 3U ) | ( bit( mHold, 13 ) << 2U ) |
                                       ( bit( mHold, 12 ) << 1U ) | bit( mHold, 15 ) );
  default:
    return 0;
  }
}

bool Igs025::write( unsigned offset, std::uint16_t value )
{
  if ( offset == 0 )
  {
    mCommand = value;
    return false;
  }
  switch ( mCommand & 0xffU )
  {
  case 0x00:
    mReg = value;
    return false;
  case 0x01: // Dragon World 3's start
    return value == 0x0002;
  case 0x02: // The Killing Blade's start
    if ( value == 0x0001 )
    {
      ++mReg;
      return true;
    }
    return false;
  case 0x03:
    mSwap = value;
    return false;
  case 0x20:
  case 0x21:
  case 0x22:
  case 0x23:
  case 0x24:
  case 0x25:
  case 0x26:
  case 0x27:
    ++mPtr;
    mHold = nextHold( mHold, mHilo, mCommand & 0xfU, static_cast<std::uint8_t>( value ) );
    return false;
  default:
    return false;
  }
}

} // namespace pgm::machine
