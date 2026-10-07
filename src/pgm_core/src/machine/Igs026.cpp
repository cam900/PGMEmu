#include "Igs026.hpp"

namespace pgm::machine
{

namespace
{

constexpr std::size_t SOUND_LATCH_1 = 1;
constexpr std::size_t Z80_CONTROL = 4;
constexpr std::size_t Z80_BUS = 5;
constexpr std::uint16_t BUS_REQUEST = 0x45d3;

constexpr std::uint32_t RAM_WINDOW = 0x10000;

} // namespace

void Igs026::reset()
{
  mLatch = {};
  mZ80Nmi = false;
}

bool Igs026::z80InReset() const
{
  return ( mLatch[Z80_CONTROL] & 1U ) != 0;
}

bool Igs026::z80BusGranted() const
{
  return mLatch[Z80_BUS] == BUS_REQUEST;
}

std::uint16_t Igs026::read( Time now, std::uint32_t address, bool upper, bool lower )
{
  if ( ( address & RAM_WINDOW ) != 0 )
  {
    if ( !z80BusGranted() )
    {
      return 0;
    }
    std::size_t const at = address & 0xfffeU;
    return static_cast<std::uint16_t>( ( mZ80Ram[at] << 8U ) | mZ80Ram[at + 1] );
  }

  switch ( address & 0xeU )
  {
  case 0x2:
    return mLatch[1];
  case 0x4:
    return mLatch[2];
  case 0x6:
    // Any cycle to the RTC's address clocks its port, whatever the strobes.
    return mRtc.access( now, false, false ) ? 1 : 0;
  case 0x8:
    return mLatch[4];
  case 0xa:
    return mLatch[5];
  case 0xc:
    return mLatch[6];
  default:
    (void)upper;
    (void)lower;
    return 0;
  }
}

void Igs026::write( Time now, std::uint32_t address, std::uint16_t value, bool upper, bool lower )
{
  if ( ( address & RAM_WINDOW ) != 0 )
  {
    if ( !z80BusGranted() )
    {
      return;
    }
    std::size_t const at = address & 0xfffeU;
    if ( upper )
    {
      mZ80Ram[at] = static_cast<std::uint8_t>( value >> 8U );
    }
    if ( lower )
    {
      mZ80Ram[at + 1] = static_cast<std::uint8_t>( value );
    }
    return;
  }

  std::size_t const reg = address & 0xeU;
  if ( reg == 0x6 )
  {
    mRtc.access( now, true, ( value & 1U ) != 0 );
    return;
  }

  // The registers take a write only when the lower strobe is active, and then
  // take the whole data bus: a byte write to an even address is lost.
  if ( !lower )
  {
    return;
  }
  switch ( reg )
  {
  case 0x2:
    mLatch[SOUND_LATCH_1] = value;
    mZ80Nmi = true;
    break;
  case 0x4:
  case 0x8:
  case 0xa:
  case 0xc:
    mLatch[reg / 2] = value;
    break;
  default:
    break;
  }
}

std::span<std::uint8_t const> Igs026::z80Ram() const
{
  return mZ80Ram;
}

} // namespace pgm::machine
