#include "Igs023.hpp"

namespace pgm::machine
{

namespace
{

// ctrl[14], the flags register at 0xB0E000.
constexpr std::uint16_t IRQ4_ENABLE = 1U << 2U;
constexpr std::uint16_t IRQ6_ENABLE = 1U << 3U;
constexpr std::size_t FLAGS = 14;
constexpr std::size_t LINE_COUNTER = 7;
constexpr std::size_t ZOOM_TABLE = 1;

constexpr int IRQ4_PERIOD_LINES = 62;

// Which dot of the raster is current after `ticks` master ticks. The RTL's
// pixel enable first fires on the fifth clock after power-up and on every
// fifth after that, and the dot counter advances on the clock that follows.
std::int64_t dotsAt( Time now )
{
  std::int64_t const ticks = masterTicks( now );
  return ticks < 1 ? 0 : ( ticks - 1 ) / 5;
}

// Where in VRAM a byte lands: the RTL folds the background map into 4 KB and
// 0x6000-0x6FFF onto the text map (vram_phys in igs023.sv).
std::size_t physical( std::size_t address )
{
  if ( ( address & 0x4000U ) == 0 )
  {
    return address & 0x0fffU;
  }
  if ( ( address & 0x3000U ) == 0x2000U )
  {
    return 0x4000U | ( address & 0x0fffU );
  }
  return address;
}

} // namespace

void Igs023::advanceTo( Time now )
{
  // Event 2k is the first dot of line k (counted from power-up), event 2k+1
  // its hsync edge. Each is seen by the logic one master tick after the dot
  // counter reaches it, which a dot's granularity does not resolve.
  std::int64_t const dots = dotsAt( now );
  std::int64_t const lines = dots / DOTS_PER_LINE;
  std::int64_t const events = ( lines * 2 ) + ( dots % DOTS_PER_LINE >= HSYNC_START_DOT ? 2 : 1 );
  while ( mEventsDone < events )
  {
    std::int64_t const event = mEventsDone++;
    if ( event % 2 == 0 )
    {
      onLineStart( static_cast<int>( ( event / 2 ) % LINES_PER_FRAME ) );
    }
    else
    {
      onHsync();
    }
  }
}

void Igs023::onLineStart( int line )
{
  if ( line == 0 && ( controlFlags() & IRQ6_ENABLE ) != 0 )
  {
    mIrq6 = true;
  }
  if ( line == VBLANK_LINES )
  {
    mControl[LINE_COUNTER] = 0;
  }
}

void Igs023::onHsync()
{
  // Sprite DMA starts here when the counter reads 221 (M3).
  ++mControl[LINE_COUNTER];
  if ( mIrq4Count == IRQ4_PERIOD_LINES - 1 )
  {
    mIrq4Count = 0;
    if ( ( controlFlags() & IRQ4_ENABLE ) != 0 )
    {
      mIrq4 = true;
    }
  }
  else
  {
    ++mIrq4Count;
  }
}

void Igs023::reset()
{
  mIrq6 = false;
  mIrq4 = false;
}

bool Igs023::irq6() const
{
  return mIrq6;
}

bool Igs023::irq4() const
{
  return mIrq4;
}

std::uint16_t Igs023::controlFlags() const
{
  return mControl[FLAGS];
}

std::uint16_t Igs023::read( Time now, std::uint32_t address, bool upper, bool lower )
{
  switch ( ( address >> 20U ) & 0x3U )
  {
  case 1: // VRAM: the upper byte of a word sits at the odd physical address.
  {
    std::size_t const word = address & 0x7ffeU;
    std::uint16_t value = 0;
    if ( upper )
    {
      value = static_cast<std::uint16_t>( mVram[physical( word | 1U )] << 8U );
    }
    if ( lower )
    {
      value = static_cast<std::uint16_t>( value | mVram[physical( word )] );
    }
    return value;
  }
  case 2: // Palette: 4K words, mirrored through the megabyte.
  {
    std::size_t const at = address & 0x1ffeU;
    return static_cast<std::uint16_t>( ( mPalette[at] << 8U ) | mPalette[at + 1] );
  }
  case 3: // Registers: one per 4 KB, the zoom table in the second.
  {
    advanceTo( now );
    std::size_t const index = ( address >> 12U ) & 0xfU;
    return index == ZOOM_TABLE ? mZoomTable[( address >> 1U ) & 0x1fU] : mControl[index];
  }
  default:
    return 0;
  }
}

void Igs023::write( Time now, std::uint32_t address, std::uint16_t value, bool upper, bool lower )
{
  auto const merge = [&]( std::uint16_t& target )
  {
    if ( upper )
    {
      target = static_cast<std::uint16_t>( ( target & 0x00ffU ) | ( value & 0xff00U ) );
    }
    if ( lower )
    {
      target = static_cast<std::uint16_t>( ( target & 0xff00U ) | ( value & 0x00ffU ) );
    }
  };

  switch ( ( address >> 20U ) & 0x3U )
  {
  case 1:
  {
    std::size_t const word = address & 0x7ffeU;
    if ( upper )
    {
      mVram[physical( word | 1U )] = static_cast<std::uint8_t>( value >> 8U );
    }
    if ( lower )
    {
      mVram[physical( word )] = static_cast<std::uint8_t>( value );
    }
    return;
  }
  case 2:
  {
    std::size_t const at = address & 0x1ffeU;
    if ( upper )
    {
      mPalette[at] = static_cast<std::uint8_t>( value >> 8U );
    }
    if ( lower )
    {
      mPalette[at + 1] = static_cast<std::uint8_t>( value );
    }
    return;
  }
  case 3:
  {
    advanceTo( now );
    std::size_t const index = ( address >> 12U ) & 0xfU;
    merge( index == ZOOM_TABLE ? mZoomTable[( address >> 1U ) & 0x1fU] : mControl[index] );
    // An interrupt stays raised until its enable is cleared.
    if ( ( controlFlags() & IRQ6_ENABLE ) == 0 )
    {
      mIrq6 = false;
    }
    if ( ( controlFlags() & IRQ4_ENABLE ) == 0 )
    {
      mIrq4 = false;
    }
    return;
  }
  default:
    return;
  }
}

int Igs023::waitStates( std::uint32_t address, bool write, bool upper, bool lower )
{
  bool const vram = ( ( address >> 20U ) & 0x3U ) == 1;
  if ( !vram )
  {
    return write ? 1 : 0;
  }
  bool const word = upper && lower;
  if ( write )
  {
    return word ? 3 : 2;
  }
  return word ? 2 : 1;
}

std::span<std::uint8_t const> Igs023::vram() const
{
  return mVram;
}

std::span<std::uint8_t const> Igs023::palette() const
{
  return mPalette;
}

int Igs023::line( Time now )
{
  return static_cast<int>( ( dotsAt( now ) / DOTS_PER_LINE ) % LINES_PER_FRAME );
}

int Igs023::dot( Time now )
{
  return static_cast<int>( dotsAt( now ) % DOTS_PER_LINE );
}

bool Igs023::vblank( Time now )
{
  return line( now ) < VBLANK_LINES;
}

bool Igs023::hblank( Time now )
{
  return dot( now ) < HBLANK_DOTS;
}

} // namespace pgm::machine
