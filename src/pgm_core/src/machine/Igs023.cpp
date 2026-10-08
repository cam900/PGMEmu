#include "Igs023.hpp"

#include <array>

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
constexpr std::size_t SCROLL_BG_Y = 2;
constexpr std::size_t SCROLL_BG_X = 3;
constexpr std::size_t SCROLL_FG_Y = 5;
constexpr std::size_t SCROLL_FG_X = 6;
constexpr std::uint16_t SPRITE_DMA_ENABLE = 1U << 0U;
constexpr std::uint16_t CPU_BUS_MASTER = 1U << 10U;

// The text layer's fetch holds VRAM from the master tick after the dot counter
// reaches 638, for 464 pulses of ce_33m: 464 * 908 / 615 master ticks.
constexpr Time FETCH_START = ( Time{ 638 } * UNITS_PER_DOT ) + UNITS_PER_MASTER_TICK;
constexpr Time FETCH_LENGTH = ( Time{ 464 } * 908 * UNITS_PER_MASTER_TICK ) / 615;
constexpr std::uint16_t SPRITE_DMA_LINE = 221;

// The dots of a line at which the raster does something: the line starts, its
// hsync rises, and the layers start fetching the next line.
constexpr std::array<int, 3> EVENT_DOTS{ 0, HSYNC_START_DOT, 638 };

// Palette words of each layer (igs023.sv's mixer).
constexpr std::uint32_t SPRITE_PALETTE = 0x000;
constexpr std::uint32_t BACKGROUND_PALETTE = 0x400;
constexpr std::uint32_t BACKDROP = 0x3ff;
constexpr std::uint32_t TEXT_PALETTE = 0x800;

// A layer pixel, as the mixer is handed it: the palette word, or NONE where
// the layer is transparent.
constexpr std::uint16_t NONE = 0xffff;

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

Igs023::Igs023( Sdram const& sdram, TileMapping tiles, std::span<std::uint8_t const> workRam )
    : mSdram{ sdram }, mTiles{ tiles }, mWorkRam{ workRam }, mSprites{ std::make_unique<SpriteFrame>() },
      mNextSprites{ std::make_unique<SpriteFrame>() }, mBuilding( static_cast<std::size_t>( WIDTH * HEIGHT * 4 ), 0 ),
      mFrame( static_cast<std::size_t>( WIDTH * HEIGHT * 4 ), 0 )
{
}

void Igs023::advanceTo( Time now )
{
  // Event 3k + n is the n-th event of line k counted from power-up. Each is
  // seen by the logic a master tick after the dot counter reaches its dot,
  // which a dot's granularity does not resolve.
  std::int64_t const dots = dotsAt( now );
  std::int64_t const lines = dots / DOTS_PER_LINE;
  int const dot = static_cast<int>( dots % DOTS_PER_LINE );
  std::int64_t passed = 0;
  for ( int const eventDot : EVENT_DOTS )
  {
    passed += dot >= eventDot ? 1 : 0;
  }
  std::int64_t const events = ( lines * 3 ) + passed;
  while ( mEventsDone < events )
  {
    std::int64_t const event = mEventsDone++;
    std::int64_t const line = event / 3;
    int const vcnt = static_cast<int>( line % LINES_PER_FRAME );
    switch ( event % 3 )
    {
    case 0:
      onLineStart( vcnt );
      break;
    case 1:
      onHsync( ( ( line * DOTS_PER_LINE ) + HSYNC_START_DOT ) * UNITS_PER_DOT );
      break;
    default:
      onFetch( vcnt );
      break;
    }
  }
}

void Igs023::onLineStart( int line )
{
  if ( line == 0 )
  {
    // Vertical blank begins: the frame just drawn is complete, and the sprites
    // the last DMA copied are those of the next.
    mFrame.swap( mBuilding );
    ++mFramesCompleted;
    if ( mNextSpritesReady )
    {
      mSprites.swap( mNextSprites );
      mNextSpritesReady = false;
    }
    if ( ( controlFlags() & IRQ6_ENABLE ) != 0 )
    {
      mIrq6 = true;
    }
  }
  if ( line == VBLANK_LINES )
  {
    mControl[LINE_COUNTER] = 0;
  }
}

void Igs023::onHsync( Time at )
{
  if ( mControl[LINE_COUNTER] == SPRITE_DMA_LINE && ( controlFlags() & SPRITE_DMA_ENABLE ) != 0 )
  {
    // The DMA takes the bus from the 68000 and reads the list four master
    // ticks a word (igs023_sprite.sv), then the sprites are drawn from it.
    mSpriteList = copySpriteList( mWorkRam );
    drawSprites( mSpriteList, mSdram, *mNextSprites );
    mNextSpritesReady = true;
    auto const words = static_cast<Time>( wordsCopied( mSpriteList ) );
    mBusHeldUntil = at + ( ( ( words * 4 ) + 4 ) * UNITS_PER_MASTER_TICK );
  }

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

void Igs023::onFetch( int line )
{
  // The fetch at the end of line `line` is for the next one.
  int const next = line + 1 - VBLANK_LINES;
  if ( next >= 0 && next < HEIGHT )
  {
    drawLine( next );
  }
}

std::uint8_t Igs023::vramAt( std::size_t address ) const
{
  return mVram[physical( address & 0x7fffU )];
}

std::uint32_t Igs023::vramWord( std::size_t address ) const
{
  // As the layers read VRAM: the low byte at the even address.
  return static_cast<std::uint32_t>( vramAt( address ) ) |
         ( static_cast<std::uint32_t>( vramAt( address + 1 ) ) << 8U );
}

std::uint32_t Igs023::tileRom( std::uint32_t address ) const
{
  address &= 0xffffffU;
  if ( mTiles.cartridge && address >= mTiles.tileBase )
  {
    return mSdram.longWord( Sdram::CART_TILES_AT + ( address - mTiles.tileBase ) );
  }
  return mSdram.longWord( Sdram::BIOS_TILES_AT + address );
}

void Igs023::drawText( int line, std::array<std::uint16_t, WIDTH>& out ) const
{
  // igs023_fg.sv: 8x8 tiles of 4 bits, a 64x32 map from 0x4000, 4 bytes a tile.
  std::uint32_t const y = ( static_cast<std::uint32_t>( line ) + mControl[SCROLL_FG_Y] ) & 0xffU;
  std::uint32_t const x = mControl[SCROLL_FG_X] & 0x1ffU;
  std::uint32_t const rowBase = 0x4000U + ( ( y >> 3U ) << 8U );
  for ( int i = 0; i < WIDTH; ++i )
  {
    std::uint32_t const at = ( x & 7U ) + static_cast<std::uint32_t>( i );
    std::uint32_t const column = ( ( x >> 3U ) + ( at >> 3U ) ) & 63U;
    std::uint32_t const entry = rowBase + ( column * 4 );
    std::uint32_t const code = vramWord( entry );
    std::uint8_t const attributes = vramAt( entry + 2 );
    bool const flipY = ( attributes & 0x80U ) != 0;
    bool const flipX = ( attributes & 0x40U ) != 0;
    std::uint32_t const row = flipY ? ( ~y & 7U ) : ( y & 7U );
    std::uint32_t const pixels = tileRom( ( code << 5U ) | ( row << 2U ) );
    std::uint32_t const pixel = flipX ? 7 - ( at & 7U ) : ( at & 7U );
    std::uint32_t const value = ( pixels >> ( pixel * 4 ) ) & 0xfU;
    out.at( static_cast<std::size_t>( i ) ) =
        value == 0xf ? NONE
                     : static_cast<std::uint16_t>( TEXT_PALETTE + ( ( ( attributes >> 1U ) & 0x1fU ) << 4U ) + value );
  }
}

void Igs023::drawBackground( int line, std::array<std::uint16_t, WIDTH>& out ) const
{
  // igs023_bg.sv: 32x32 tiles of 5 bits, 20 bytes a row, a 64-column map at
  // 0x0000 folded into 4 KB, and a scroll word per line at 0x7000.
  std::uint32_t const y = ( static_cast<std::uint32_t>( line ) + mControl[SCROLL_BG_Y] ) & 0x7ffU;
  std::uint32_t const scrollAt = 0x7000U + ( static_cast<std::uint32_t>( line ) << 1U );
  std::uint32_t const scroll = vramWord( scrollAt );
  std::uint32_t const x = ( mControl[SCROLL_BG_X] + scroll ) & 0x7ffU;
  std::uint32_t const rowBase = ( y >> 5U ) << 8U;
  for ( int i = 0; i < WIDTH; ++i )
  {
    std::uint32_t const at = ( x & 31U ) + static_cast<std::uint32_t>( i );
    std::uint32_t const column = ( ( x >> 5U ) + ( at >> 5U ) ) & 63U;
    std::uint32_t const entry = rowBase + ( column * 4 );
    std::uint32_t const code = vramWord( entry ) & 0x7fffU;
    std::uint8_t const attributes = vramAt( entry + 2 );
    bool const flipY = ( attributes & 0x80U ) != 0;
    bool const flipX = ( attributes & 0x40U ) != 0;
    std::uint32_t const row = flipY ? ( ~y & 31U ) : ( y & 31U );
    std::uint32_t const pixel = flipX ? 31 - ( at & 31U ) : ( at & 31U );
    // The row is a stream of 160 bits, five pixels to a 5-bit group, lowest
    // first, held in five 32-bit words.
    std::uint32_t const rowAddress = ( ( code << 5U ) | row ) * 20;
    std::uint32_t const bit = pixel * 5;
    std::uint64_t const words =
        tileRom( rowAddress + ( ( bit >> 5U ) * 4 ) ) |
        ( static_cast<std::uint64_t>( tileRom( rowAddress + ( ( ( bit >> 5U ) + 1 ) * 4 ) ) ) << 32U );
    std::uint32_t const value = static_cast<std::uint32_t>( words >> ( bit & 31U ) ) & 0x1fU;
    out.at( static_cast<std::size_t>( i ) ) =
        value == 0x1f
            ? NONE
            : static_cast<std::uint16_t>( BACKGROUND_PALETTE + ( ( ( attributes >> 1U ) & 0x1fU ) << 5U ) + value );
  }
}

void Igs023::drawLine( int line )
{
  std::array<std::uint16_t, WIDTH> text{};
  std::array<std::uint16_t, WIDTH> background{};
  drawText( line, text );
  drawBackground( line, background );
  SpriteLine const& sprites = mSprites->at( static_cast<std::size_t>( line ) );

  std::size_t at = static_cast<std::size_t>( line ) * WIDTH * 4;
  for ( std::size_t i = 0; i < WIDTH; ++i )
  {
    // FG over high-priority sprites over BG over low-priority sprites.
    std::uint16_t const sprite = sprites[i];
    bool const spriteDrawn = ( sprite & 0x800U ) != 0;
    bool const spriteHigh = ( sprite & 0x400U ) == 0;
    std::uint32_t word = BACKDROP;
    if ( text[i] != NONE )
    {
      word = text[i];
    }
    else if ( spriteDrawn && ( spriteHigh || background[i] == NONE ) )
    {
      word = SPRITE_PALETTE + ( sprite & 0x3ffU );
    }
    else if ( background[i] != NONE )
    {
      word = background[i];
    }

    // xRGB555, each 5-bit channel widened as PGM.sv widens it.
    std::uint32_t const colour = ( static_cast<std::uint32_t>( mPalette[std::size_t{ word } * 2] ) << 8U ) |
                                 mPalette[( std::size_t{ word } * 2 ) + 1];
    for ( unsigned const shift : { 10U, 5U, 0U } )
    {
      std::uint32_t const channel = ( colour >> shift ) & 0x1fU;
      mBuilding[at++] = static_cast<std::uint8_t>( ( channel << 3U ) | ( channel >> 2U ) );
    }
    mBuilding[at++] = 0xff;
  }
}

std::span<std::uint8_t const> Igs023::frame() const
{
  return mFrame;
}

std::int64_t Igs023::framesCompleted() const
{
  return mFramesCompleted;
}

Time Igs023::busHeldUntil() const
{
  return mBusHeldUntil;
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

Time Igs023::vramFreeAt( Time now ) const
{
  if ( ( controlFlags() & CPU_BUS_MASTER ) != 0 )
  {
    return now;
  }
  Time const line = now / UNITS_PER_LINE;
  for ( Time const fetchLine : { line, line - 1 } )
  {
    auto const vcnt = static_cast<int>( ( ( fetchLine % LINES_PER_FRAME ) + LINES_PER_FRAME ) % LINES_PER_FRAME );
    if ( fetchLine < 0 || vcnt < VBLANK_LINES - 1 || vcnt >= LINES_PER_FRAME - 1 )
    {
      continue;
    }
    Time const start = ( fetchLine * UNITS_PER_LINE ) + FETCH_START;
    if ( now >= start && now < start + FETCH_LENGTH )
    {
      return start + FETCH_LENGTH;
    }
  }
  return now;
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
