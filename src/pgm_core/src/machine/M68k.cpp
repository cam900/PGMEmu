#include "M68k.hpp"

#include <array>

namespace pgm::machine
{

namespace
{

/// Cycles from the start of interrupt processing to the acknowledge cycle.
constexpr Time ACKNOWLEDGE_AFTER = 10;

} // namespace

M68k::M68k( Bus68k& bus, Time& time ) : mBus{ &bus }, mTime{ &time }
{
  setModel( moira::Model::M68000 );
}

bool M68k::stopped() const
{
  return ( flags & moira::State::STOPPED ) != 0;
}

std::string M68k::disassembleAt( std::uint32_t address, int& length ) const
{
  std::array<char, 128> text{};
  length = disassemble( text.data(), address );
  return text.data();
}

void M68k::startEClock( Time at )
{
  mEClockOrigin = at;
}

void M68k::willInterrupt( moira::u8 /*level*/ )
{
  // PGM.sv answers every interrupt acknowledge with VPA, so the 68000 runs it
  // as a 6800 cycle (fx68k.sv): it asserts VMA when its E counter, which counts
  // CPU cycles 0 to 9 from reset, reaches 3, and ends the cycle when it reaches
  // 8 and the rest of the cycle has passed. Moira charges the acknowledge 4
  // cycles; the 6800 cycle takes 7 plus the wait for the counter to reach 3.
  // The acknowledge starts 10 cycles from here, after an internal 6 and the
  // stacking of the PC's low word.
  Time const cycles = ( ( *mTime - mEClockOrigin ) / UNITS_PER_M68K_CYCLE ) + ACKNOWLEDGE_AFTER;
  auto const phase = static_cast<int>( cycles % 10 );
  int const waitForVma = ( 3 - phase + 10 ) % 10;
  sync( waitForVma + 3 );
}

void M68k::sync( int cycles )
{
  clock += cycles;
  *mTime += static_cast<Time>( cycles ) * UNITS_PER_M68K_CYCLE;
}

moira::u8 M68k::read8( moira::u32 addr ) const
{
  bool const upper = ( addr & 1U ) == 0;
  std::uint16_t const word = mBus->read( addr, upper, !upper );
  return static_cast<moira::u8>( upper ? word >> 8U : word );
}

moira::u16 M68k::read16( moira::u32 addr ) const
{
  return mBus->read( addr, true, true );
}

moira::u32 M68k::read32( moira::u32 addr ) const
{
  // Only a 32-bit port is read in one cycle, and the 68000 has none: Moira
  // splits a long into two word cycles itself. This answers for completeness.
  return ( static_cast<moira::u32>( read16( addr ) ) << 16U ) | read16( addr + 2 );
}

moira::u16 M68k::read16Dasm( moira::u32 addr ) const
{
  return mBus->peek( addr );
}

void M68k::write8( moira::u32 addr, moira::u8 val ) const
{
  bool const upper = ( addr & 1U ) == 0;
  // The 68000 drives a written byte on both halves of the data bus.
  auto const word = static_cast<std::uint16_t>( ( val << 8U ) | val );
  mBus->write( addr, word, upper, !upper );
}

void M68k::write16( moira::u32 addr, moira::u16 val ) const
{
  mBus->write( addr, val, true, true );
}

void M68k::write32( moira::u32 addr, moira::u32 val ) const
{
  write16( addr, static_cast<moira::u16>( val >> 16U ) );
  write16( addr + 2, static_cast<moira::u16>( val ) );
}

} // namespace pgm::machine
