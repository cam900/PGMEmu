#include "Asic3.hpp"

#include <bit>

namespace pgm::machine
{

namespace
{

std::uint16_t bit( std::uint32_t value, unsigned index )
{
  return static_cast<std::uint16_t>( ( value >> index ) & 1U );
}

} // namespace

Asic3::Asic3( std::uint8_t region ) : mRegion{ static_cast<std::uint8_t>( region & 7U ) } {}

void Asic3::reset()
{
  mRegister = 0;
  mLatch = {};
  mX = 0;
  mHilo = 0;
  mHold = 0;
}

std::uint16_t Asic3::read() const
{
  switch ( mRegister )
  {
  case 0x00:
    return static_cast<std::uint16_t>( ( mLatch[0] & 0xf7U ) | ( ( static_cast<unsigned>( mRegion ) << 3U ) & 0x08U ) );
  case 0x01:
    return mLatch[1];
  case 0x02:
    return static_cast<std::uint16_t>( ( mLatch[2] & 0x7fU ) | ( ( static_cast<unsigned>( mRegion ) << 6U ) & 0x80U ) );
  case 0x03:
    return static_cast<std::uint16_t>(
        ( bit( mHold, 5 ) << 7U ) | ( bit( mHold, 2 ) << 6U ) | ( bit( mHold, 9 ) << 5U ) | ( bit( mHold, 7 ) << 4U ) |
        ( bit( mHold, 10 ) << 3U ) | ( bit( mHold, 13 ) << 2U ) | ( bit( mHold, 12 ) << 1U ) | bit( mHold, 15 ) );
  // The chip's signature and self-test constants: "IGS" and a few more.
  case 0x20:
    return 0x49;
  case 0x21:
    return 0x47;
  case 0x22:
    return 0x53;
  case 0x24:
  case 0x25:
  case 0x27:
  case 0x28:
  case 0x2b:
    return 0x41;
  case 0x26:
    return 0x7f;
  case 0x2a:
    return 0x3e;
  case 0x2c:
  case 0x31:
  case 0x32:
  case 0x33:
    return 0x49;
  case 0x2d:
    return 0xf9;
  case 0x2e:
    return 0x0a;
  case 0x30:
    return 0x26;
  case 0x34:
    return 0x32;
  default:
    return 0;
  }
}

void Asic3::write( std::uint32_t address, std::uint16_t value )
{
  if ( ( address & 0xeU ) == 0 )
  {
    mRegister = static_cast<std::uint8_t>( value );
    return;
  }
  switch ( mRegister )
  {
  case 0x00:
  case 0x01:
  case 0x02:
    mLatch.at( mRegister ) = static_cast<std::uint8_t>( ( value & 0x7fU ) << 1U );
    break;
  case 0x40:
    mHilo = static_cast<std::uint16_t>( ( mHilo << 8U ) | value );
    break;
  case 0x48:
    mX = static_cast<std::uint8_t>( ( ( mHilo & 0x0a00U ) == 0 ? 8U : 0U ) | ( ( mHilo & 0x9000U ) == 0 ? 4U : 0U ) |
                                    ( ( mHilo & 0x0006U ) == 0 ? 2U : 0U ) | ( ( mHilo & 0x0090U ) == 0 ? 1U : 0U ) );
    break;
  case 0x80:
  case 0x81:
  case 0x82:
  case 0x83:
  case 0x84:
  case 0x85:
  case 0x86:
  case 0x87:
    mHold = nextHold( value );
    break;
  case 0xa0:
    mHold = 0;
    break;
  default:
    break;
  }
}

std::uint16_t Asic3::nextHold( std::uint16_t data ) const
{
  std::uint32_t next = std::rotl( mHold, 1 );
  next ^= 0x2badU;
  next ^= bit( data, mRegister & 7U );
  next ^= static_cast<std::uint32_t>( bit( mX, 2 ) ) << 10U;
  next ^= bit( mHold, 5 );
  switch ( mRegion )
  {
  case 0:
  case 1:
    next ^= bit( mHold, 10 ) ^ bit( mHold, 8 );
    next ^= ( static_cast<std::uint32_t>( bit( mX, 0 ) ) << 1U ) |
            ( static_cast<std::uint32_t>( bit( mX, 1 ) ) << 6U ) |
            ( static_cast<std::uint32_t>( bit( mX, 3 ) ) << 14U );
    break;
  case 2:
    next ^= bit( mHold, 10 ) ^ bit( mHold, 8 );
    next ^= ( static_cast<std::uint32_t>( bit( mX, 0 ) ) << 4U ) |
            ( static_cast<std::uint32_t>( bit( mX, 1 ) ) << 6U ) |
            ( static_cast<std::uint32_t>( bit( mX, 3 ) ) << 12U );
    break;
  case 3:
    next ^= bit( mHold, 7 ) ^ bit( mHold, 6 );
    next ^= ( static_cast<std::uint32_t>( bit( mX, 0 ) ) << 4U ) |
            ( static_cast<std::uint32_t>( bit( mX, 1 ) ) << 6U ) |
            ( static_cast<std::uint32_t>( bit( mX, 3 ) ) << 12U );
    break;
  case 4:
    next ^= bit( mHold, 7 ) ^ bit( mHold, 6 );
    next ^= ( static_cast<std::uint32_t>( bit( mX, 0 ) ) << 3U ) |
            ( static_cast<std::uint32_t>( bit( mX, 1 ) ) << 8U ) |
            ( static_cast<std::uint32_t>( bit( mX, 3 ) ) << 14U );
    break;
  default:
    break;
  }
  return static_cast<std::uint16_t>( next );
}

} // namespace pgm::machine
