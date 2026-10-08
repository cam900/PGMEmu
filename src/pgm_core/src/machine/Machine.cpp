#include "pgm/machine/Machine.hpp"

#include "Bus68k.hpp"
#include "Igs023.hpp"
#include "Igs026.hpp"
#include "M68k.hpp"

#include <algorithm>
#include <optional>

namespace pgm::machine
{

namespace
{

constexpr std::int64_t POWER_ON_RESET_TICKS = 100;

/// Where the RTL simulator's `vblank` output rises, after line 0 begins: the
/// first pixel enable (5 master ticks), the output's two pixel delays and the
/// edge's own tick, 11 ticks in all.
constexpr Time FRAME_BOUNDARY_OFFSET = 11 * UNITS_PER_MASTER_TICK;

/// The first frame boundary strictly after `now`.
Time nextFrameBoundary( Time now )
{
  if ( now < FRAME_BOUNDARY_OFFSET )
  {
    return FRAME_BOUNDARY_OFFSET;
  }
  return ( ( ( ( now - FRAME_BOUNDARY_OFFSET ) / UNITS_PER_FRAME ) + 1 ) * UNITS_PER_FRAME ) + FRAME_BOUNDARY_OFFSET;
}

/// The next time the raster can change an interrupt: a line start or an hsync.
Time nextRasterEvent( Time now )
{
  Time const line = now / UNITS_PER_LINE * UNITS_PER_LINE;
  Time const hsync = line + ( HSYNC_START_DOT * UNITS_PER_DOT );
  return now < hsync ? hsync : line + UNITS_PER_LINE;
}

/// The level on the 68000's interrupt lines: 6 for vblank, over 4 for the
/// 62-line interrupt, as PGM.sv encodes them.
moira::u8 interruptLevel( Igs023 const& video )
{
  if ( video.irq6() )
  {
    return 6;
  }
  return video.irq4() ? 4 : 0;
}

std::span<std::uint8_t const> romOf( cart::PgmImage const* cartridge, cart::RomType type )
{
  if ( cartridge == nullptr )
  {
    return {};
  }
  auto const rom = cartridge->rom( type );
  return rom ? rom->data : std::span<std::uint8_t const>{};
}

std::uint32_t mappingOf( cart::PgmImage const* cartridge, cart::RomType type )
{
  if ( cartridge == nullptr )
  {
    return 0;
  }
  auto const rom = cartridge->rom( type );
  return rom ? rom->mapping : 0;
}

/// The region ASIC3 reports: the cartridge's default when its region block is
/// ASIC3's, the world otherwise, which is what the RTL wires in for every game.
std::uint8_t asic3Region( cart::PgmImage const* cartridge )
{
  if ( cartridge == nullptr || !cartridge->regionInfo() ||
       cartridge->regionInfo()->scheme != cart::RegionScheme::ASIC3 )
  {
    return 0;
  }
  return static_cast<std::uint8_t>( cartridge->regionInfo()->defaultRegion );
}

Sdram sdramOf( cart::Bios const& bios, cart::PgmImage const* cartridge )
{
  return Sdram{ .biosProgram = bios.program().data,
                .biosTiles = bios.tiles().data,
                .biosMusic = bios.music().data,
                .cartProgram = romOf( cartridge, cart::RomType::PRG ),
                .cartTiles = romOf( cartridge, cart::RomType::TLE ),
                .cartMusic = romOf( cartridge, cart::RomType::AUD ),
                .cartBRom = romOf( cartridge, cart::RomType::SPM ),
                .cartARom = romOf( cartridge, cart::RomType::SPC ) };
}

} // namespace

struct Machine::Parts
{
  Parts( cart::Bios const& bios, cart::PgmImage const* cartridge )
      : sdram{ sdramOf( bios, cartridge ) },
        video{ sdram,
               TileMapping{ .cartridge = cartridge != nullptr, .tileBase = mappingOf( cartridge, cart::RomType::TLE ) },
               workRam },
        asic3{ asic3Region( cartridge ) },
        bus{ RomSpace{ .sdram = &sdram,
                       .cartridge = cartridge != nullptr,
                       .cartBase = mappingOf( cartridge, cart::RomType::PRG ) },
             BusDevices{ .video = video, .io = io, .asic3 = asic3, .inputs = inputs },
             now,
             workRam },
        cpu{ bus, now }
  {
  }

  /// Runs until `until`, or until `condition` holds after an instruction.
  RunResult run( Time until, std::function<bool()> const* condition );

  Time now{};
  /// The reset line is held until this time; the 68000 starts when it is let go.
  Time resetReleasedAt{};
  bool cpuStarted{};
  /// The breakpoint a run stopped at, which the next run executes rather than
  /// stopping at again.
  std::optional<std::uint32_t> resumeFrom;
  std::set<std::uint32_t> breakpoints;
  Sdram sdram;
  std::array<std::uint8_t, 0x20000> workRam{};
  InputPorts inputs;
  Igs023 video;
  Igs026 io;
  Asic3 asic3;
  Bus68k bus;
  M68k cpu;
};

RunResult Machine::Parts::run( Time until, std::function<bool()> const* condition )
{
  Time const start = now;
  std::int64_t const startFrame =
      start < FRAME_BOUNDARY_OFFSET ? 0 : ( ( start - FRAME_BOUNDARY_OFFSET ) / UNITS_PER_FRAME ) + 1;
  auto const result = [&]( StopReason reason )
  {
    std::int64_t const endFrame =
        now < FRAME_BOUNDARY_OFFSET ? 0 : ( ( now - FRAME_BOUNDARY_OFFSET ) / UNITS_PER_FRAME ) + 1;
    return RunResult{ .reason = reason,
                      .ticks = masterTicks( now ) - masterTicks( start ),
                      .frames = endFrame - startFrame };
  };

  while ( now < until )
  {
    if ( !cpuStarted )
    {
      if ( now < resetReleasedAt )
      {
        now = std::min( resetReleasedAt, until );
        video.advanceTo( now );
        continue;
      }
      cpu.startEClock( now );
      cpu.reset();
      cpuStarted = true;
      continue;
    }

    video.advanceTo( now );
    if ( now < video.busHeldUntil() )
    {
      // Sprite DMA has the bus; the 68000 waits for it.
      now = std::min( video.busHeldUntil(), until );
      continue;
    }
    cpu.setIPL( interruptLevel( video ) );

    std::uint32_t const pc = cpu.getPC0();
    if ( !breakpoints.empty() && breakpoints.contains( pc ) && resumeFrom != pc )
    {
      resumeFrom = pc;
      return result( StopReason::BREAKPOINT );
    }
    resumeFrom.reset();

    if ( cpu.stopped() && !video.irq6() && !video.irq4() )
    {
      // Nothing can wake a stopped 68000 before the raster raises an interrupt,
      // so time moves straight there rather than an instruction at a time.
      now = std::min( nextRasterEvent( now ), until );
      continue;
    }

    cpu.execute();
    if ( cpu.isHalted() )
    {
      return result( StopReason::HALTED );
    }
    if ( condition != nullptr && ( *condition )() )
    {
      return result( StopReason::CONDITION_MET );
    }
  }
  video.advanceTo( now );
  return result( condition != nullptr ? StopReason::TIMEOUT : StopReason::COMPLETED );
}

Machine::Machine( cart::Bios const& bios, cart::PgmImage const* cartridge )
    : mParts{ std::make_unique<Parts>( bios, cartridge ) }
{
  mParts->resetReleasedAt = POWER_ON_RESET_TICKS * UNITS_PER_MASTER_TICK;
}

Machine::~Machine() = default;

void Machine::reset( std::int64_t masterTicks )
{
  Parts& parts = *mParts;
  parts.io.reset();
  parts.video.reset();
  parts.asic3.reset();
  parts.now += masterTicks * UNITS_PER_MASTER_TICK;
  parts.video.advanceTo( parts.now );
  parts.resetReleasedAt = parts.now;
  parts.cpuStarted = false;
  parts.resumeFrom.reset();
}

RunResult Machine::runFrames( std::int64_t frames )
{
  Time until = mParts->now;
  for ( std::int64_t i = 0; i < frames; ++i )
  {
    until = nextFrameBoundary( until );
  }
  return mParts->run( until, nullptr );
}

RunResult Machine::runTicks( std::int64_t masterTicks )
{
  return mParts->run( mParts->now + ( masterTicks * UNITS_PER_MASTER_TICK ), nullptr );
}

RunResult Machine::runUntil( std::function<bool()> const& condition, std::int64_t timeoutTicks )
{
  return mParts->run( mParts->now + ( timeoutTicks * UNITS_PER_MASTER_TICK ), &condition );
}

Time Machine::now() const
{
  return mParts->now;
}

std::int64_t Machine::frame() const
{
  Time const now = mParts->now;
  return now < FRAME_BOUNDARY_OFFSET ? 0 : ( ( now - FRAME_BOUNDARY_OFFSET ) / UNITS_PER_FRAME ) + 1;
}

int Machine::line() const
{
  return Igs023::line( mParts->now );
}

int Machine::dot() const
{
  return Igs023::dot( mParts->now );
}

bool Machine::vblank() const
{
  return Igs023::vblank( mParts->now );
}

bool Machine::hblank() const
{
  return Igs023::hblank( mParts->now );
}

M68kState Machine::m68kState() const
{
  M68k const& cpu = mParts->cpu;
  M68kState state;
  for ( int i = 0; i < 8; ++i )
  {
    state.d.at( static_cast<std::size_t>( i ) ) = cpu.getD( i );
    state.a.at( static_cast<std::size_t>( i ) ) = cpu.getA( i );
  }
  state.pc = cpu.getPC0();
  state.sr = cpu.getSR();
  state.usp = cpu.getUSP();
  state.ssp = cpu.getISP();
  state.stopped = cpu.stopped();
  state.halted = cpu.isHalted();
  return state;
}

std::string Machine::disassemble( std::uint32_t address, int& length ) const
{
  return mParts->cpu.disassembleAt( address, length );
}

std::uint16_t Machine::peek( std::uint32_t address ) const
{
  return mParts->bus.peek( address );
}

void Machine::addBreakpoint( std::uint32_t address )
{
  mParts->breakpoints.insert( address );
}

void Machine::removeBreakpoint( std::uint32_t address )
{
  mParts->breakpoints.erase( address );
}

std::set<std::uint32_t> const& Machine::breakpoints() const
{
  return mParts->breakpoints;
}

void Machine::setInputs( std::array<std::uint16_t, 4> const& pressed )
{
  mParts->inputs.pressed = pressed;
}

std::span<std::uint8_t const> Machine::workRam() const
{
  return mParts->bus.workRam();
}

std::span<std::uint8_t const> Machine::videoRam() const
{
  return mParts->video.vram();
}

std::span<std::uint8_t const> Machine::paletteRam() const
{
  return mParts->video.palette();
}

std::span<std::uint8_t const> Machine::picture() const
{
  return mParts->video.frame();
}

std::int64_t Machine::picturesDrawn() const
{
  return mParts->video.framesCompleted();
}

std::span<std::uint8_t const> Machine::z80Ram() const
{
  return mParts->io.z80Ram();
}

} // namespace pgm::machine
