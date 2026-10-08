#include "pgm/machine/Machine.hpp"

#include "Bus68k.hpp"
#include "ClockEnables.hpp"
#include "Ics2115.hpp"
#include "Igs023.hpp"
#include "Igs026.hpp"
#include "M68k.hpp"
#include "StateArchive.hpp"
#include "Z80.hpp"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

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

/// What a save state begins with, and the version of its layout, which
/// changes whenever a part's state does.
constexpr std::uint32_t STATE_MAGIC = 0x54534750; // "PGST"
constexpr std::uint32_t STATE_VERSION = 1;

} // namespace

struct Machine::Parts
{
  Parts( cart::Bios const& bios, cart::PgmImage const* cartridge )
      : sdram{ sdramOf( bios, cartridge ) },
        video{ sdram,
               TileMapping{ .cartridge = cartridge != nullptr, .tileBase = mappingOf( cartridge, cart::RomType::TLE ) },
               workRam },
        ics2115{ sdram,
                 SampleMapping{ .cartridge = cartridge != nullptr,
                                .musicBase = mappingOf( cartridge, cart::RomType::AUD ) } },
        io{ z80, ics2115 }, asic3{ asic3Region( cartridge ) },
        bus{ RomSpace{ .sdram = &sdram,
                       .cartridge = cartridge != nullptr,
                       .cartBase = mappingOf( cartridge, cart::RomType::PRG ) },
             BusDevices{ .video = video, .io = io, .asic3 = asic3, .inputs = inputs },
             now,
             workRam },
        cpu{ bus, now }
  {
    cpu.watch( watch );
  }

  /// Runs until `until`, or until `condition` holds after an instruction.
  RunResult run( Time until, std::function<bool()> const* condition );

  template <class Archive>
  void serialize( Archive& archive )
  {
    archive( now );
    archive( resetReleasedAt );
    archive( cpuStarted );
    archive( workRam );
    archive( inputs.pressed );
    video.serialize( archive );
    z80.serialize( archive );
    ics2115.serialize( archive );
    io.serialize( archive );
    asic3.serialize( archive );
    cpu.serialize( archive );
  }

  Time now{};
  /// The reset line is held until this time; the 68000 starts when it is let go.
  Time resetReleasedAt{};
  bool cpuStarted{};
  /// The breakpoint a run stopped at, which the next run executes rather than
  /// stopping at again.
  std::optional<std::uint32_t> resumeFrom;
  std::set<std::uint32_t> breakpoints;
  WatchState watch;
  /// The last instructions executed, a ring: traceNext is where the next goes.
  std::array<TraceEntry, Machine::TRACE_SIZE> traceRing{};
  std::size_t traceNext{};
  std::size_t traceCount{};
  /// The sound produced by the last run, and who hears it as the run ends.
  std::vector<AudioFrame> audio;
  std::function<void( std::span<AudioFrame const> )> audioListener;
  Sdram sdram;
  std::array<std::uint8_t, 0x20000> workRam{};
  InputPorts inputs;
  Igs023 video;
  Z80 z80;
  Ics2115 ics2115;
  Igs026 io;
  Asic3 asic3;
  Bus68k bus;
  M68k cpu;
};

RunResult Machine::Parts::run( Time until, std::function<bool()> const* condition )
{
  audio.clear();
  watch.hit.reset();
  Time const start = now;
  std::int64_t const startFrame =
      start < FRAME_BOUNDARY_OFFSET ? 0 : ( ( start - FRAME_BOUNDARY_OFFSET ) / UNITS_PER_FRAME ) + 1;
  auto const result = [&]( StopReason reason )
  {
    // The sound side has only been caught up as far as the 68000 last
    // reached it.
    io.advanceTo( now );
    ics2115.takeFrames( audio );
    if ( audioListener )
    {
      audioListener( audio );
    }
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

    traceRing.at( traceNext ) = TraceEntry{ .pc = pc, .ticks = masterTicks( now ) };
    traceNext = ( traceNext + 1 ) % traceRing.size();
    traceCount = std::min( traceCount + 1, traceRing.size() );
    cpu.execute();
    if ( cpu.isHalted() )
    {
      return result( StopReason::HALTED );
    }
    if ( watch.hit )
    {
      // Moira has moved its PC on by the time it writes; the instruction is
      // the one this loop started.
      watch.hit->pc = pc;
      return result( StopReason::WATCHPOINT );
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
  parts.audio.clear();
  parts.io.reset( parts.now );
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

std::vector<std::uint8_t> Machine::saveState()
{
  StateWriter writer;
  std::uint32_t magic = STATE_MAGIC;
  std::uint32_t version = STATE_VERSION;
  writer( magic );
  writer( version );
  mParts->serialize( writer );
  return writer.bytes();
}

bool Machine::loadState( std::span<std::uint8_t const> state )
{
  // Read into a copy first, so that a state that turns out short or long
  // leaves this machine as it was.
  StateReader check{ state };
  std::uint32_t magic = 0;
  std::uint32_t version = 0;
  check( magic );
  check( version );
  if ( !check.good() || magic != STATE_MAGIC || version != STATE_VERSION )
  {
    return false;
  }
  std::vector<std::uint8_t> const before = saveState();
  mParts->serialize( check );
  if ( !check.good() || !check.atEnd() )
  {
    StateReader restore{ before };
    restore( magic );
    restore( version );
    mParts->serialize( restore );
    return false;
  }
  mParts->resumeFrom.reset();
  mParts->audio.clear();
  return true;
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

void Machine::addWatchpoint( Watchpoint watchpoint )
{
  watchpoint.address &= 0xffffffU;
  std::erase_if( mParts->watch.points, [&]( Watchpoint const& point ) { return point.address == watchpoint.address; } );
  mParts->watch.points.push_back( watchpoint );
}

void Machine::removeWatchpoint( std::uint32_t address )
{
  std::erase_if( mParts->watch.points,
                 [address]( Watchpoint const& point ) { return point.address == ( address & 0xffffffU ); } );
}

std::vector<Watchpoint> const& Machine::watchpoints() const
{
  return mParts->watch.points;
}

std::optional<WatchpointHit> const& Machine::watchpointHit() const
{
  return mParts->watch.hit;
}

std::vector<TraceEntry> Machine::trace( std::size_t count ) const
{
  Parts const& parts = *mParts;
  count = std::min( count, parts.traceCount );
  std::vector<TraceEntry> entries;
  entries.reserve( count );
  for ( std::size_t i = count; i > 0; --i )
  {
    entries.push_back(
        parts.traceRing.at( ( parts.traceNext + parts.traceRing.size() - i ) % parts.traceRing.size() ) );
  }
  return entries;
}

void Machine::setInputs( std::array<std::uint16_t, 4> const& pressed )
{
  mParts->inputs.pressed = pressed;
}

void Machine::setWorkRam( std::span<std::uint8_t const> bytes )
{
  std::ranges::copy( bytes.first( std::min( bytes.size(), mParts->workRam.size() ) ), mParts->workRam.begin() );
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

Z80Registers Machine::z80Registers() const
{
  return mParts->z80.registers();
}

bool Machine::z80Halted() const
{
  return mParts->z80.halted();
}

std::span<AudioFrame const> Machine::audio() const
{
  return mParts->audio;
}

double Machine::audioRate() const
{
  return CE_33M_HZ / static_cast<double>( mParts->ics2115.samplePeriod() );
}

void Machine::setAudioListener( std::function<void( std::span<AudioFrame const> )> listener )
{
  mParts->audioListener = std::move( listener );
}

std::array<Ics2115Voice, 32> const& Machine::ics2115Voices() const
{
  return mParts->ics2115.voices();
}

std::size_t Machine::ics2115ActiveVoices() const
{
  return mParts->ics2115.activeVoices();
}

std::span<std::uint8_t const> Machine::z80Ram() const
{
  return mParts->io.z80Ram();
}

} // namespace pgm::machine
