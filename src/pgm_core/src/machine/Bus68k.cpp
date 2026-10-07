#include "Bus68k.hpp"

namespace pgm::machine
{

namespace
{

constexpr std::uint32_t ADDRESS_MASK = 0xffffffU;

// The BIOS's SDRAM layout, system_consts.sv.
constexpr std::uint32_t BIOS_TILES_AT = 0x100000;
constexpr std::uint32_t BIOS_MUSIC_AT = 0x300000;

std::uint16_t wordOf( std::span<std::uint8_t const> rom, std::uint32_t offset )
{
  if ( offset + 1 >= rom.size() )
  {
    return 0;
  }
  return static_cast<std::uint16_t>( rom[offset] | ( rom[offset + 1] << 8U ) );
}

} // namespace

std::uint16_t RomSpace::word( std::uint32_t address ) const
{
  if ( !cartProgram.empty() && address >= cartBase )
  {
    return wordOf( cartProgram, address - cartBase );
  }
  if ( address < BIOS_TILES_AT )
  {
    return wordOf( biosProgram, address );
  }
  if ( address < BIOS_MUSIC_AT )
  {
    return wordOf( biosTiles, address - BIOS_TILES_AT );
  }
  return wordOf( biosMusic, address - BIOS_MUSIC_AT );
}

Bus68k::Bus68k( RomSpace rom, Igs023& video, Igs026& io, InputPorts const& inputs, Time& time )
    : mRom{ rom }, mVideo{ video }, mIo{ io }, mInputs{ inputs }, mTime{ time }
{
}

std::uint16_t Bus68k::read( std::uint32_t address, bool upper, bool lower )
{
  address &= ADDRESS_MASK;
  switch ( address >> 20U )
  {
  case 0x0:
  case 0x1:
  case 0x2:
  case 0x3:
  case 0x4:
  case 0x5:
  case 0x6:
  case 0x7:
    return mRom.word( address & ~1U );
  case 0x8:
  {
    std::size_t const at = address & 0x1fffeU;
    return static_cast<std::uint16_t>( ( mWorkRam[at] << 8U ) | mWorkRam[at + 1] );
  }
  case 0x9:
  case 0xa:
  case 0xb:
  {
    std::uint16_t const value = mVideo.read( mTime, address, upper, lower );
    mTime += Igs023::waitStates( address, false, upper, lower ) * UNITS_PER_M68K_CYCLE;
    return value;
  }
  case 0xc:
    if ( ( address & 0xffff00U ) == 0xc08000U )
    {
      // The inputs are active low.
      return static_cast<std::uint16_t>( ~mInputs.pressed[( address >> 1U ) & 0x3U] );
    }
    if ( ( address & 0xfffff0U ) == 0xc04000U )
    {
      return 0; // ASIC3 (M7)
    }
    if ( ( address & 0xfe0000U ) == 0xc00000U )
    {
      return mIo.read( mTime, address, upper, lower );
    }
    return 0;
  default:
    return 0;
  }
}

void Bus68k::write( std::uint32_t address, std::uint16_t value, bool upper, bool lower )
{
  address &= ADDRESS_MASK;
  switch ( address >> 20U )
  {
  case 0x8:
  {
    std::size_t const at = address & 0x1fffeU;
    if ( upper )
    {
      mWorkRam[at] = static_cast<std::uint8_t>( value >> 8U );
    }
    if ( lower )
    {
      mWorkRam[at + 1] = static_cast<std::uint8_t>( value );
    }
    return;
  }
  case 0x9:
  case 0xa:
  case 0xb:
    mVideo.write( mTime, address, value, upper, lower );
    mTime += Igs023::waitStates( address, true, upper, lower ) * UNITS_PER_M68K_CYCLE;
    return;
  case 0xc:
    if ( ( address & 0xffff00U ) == 0xc08000U || ( address & 0xfffff0U ) == 0xc04000U )
    {
      return; // The inputs are read-only; ASIC3 is M7's.
    }
    if ( ( address & 0xfe0000U ) == 0xc00000U )
    {
      mIo.write( mTime, address, value, upper, lower );
    }
    return;
  default:
    return; // ROM, and nothing.
  }
}

std::uint16_t Bus68k::peek( std::uint32_t address ) const
{
  address &= ADDRESS_MASK & ~1U;
  if ( address < 0x800000U )
  {
    return mRom.word( address );
  }
  if ( ( address >> 20U ) == 0x8 )
  {
    std::size_t const at = address & 0x1fffeU;
    return static_cast<std::uint16_t>( ( mWorkRam[at] << 8U ) | mWorkRam[at + 1] );
  }
  return 0;
}

std::span<std::uint8_t const> Bus68k::workRam() const
{
  return mWorkRam;
}

} // namespace pgm::machine
