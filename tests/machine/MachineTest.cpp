#include <catch2/catch_test_macros.hpp>

#include "support/Expect.hpp"
#include "support/Files.hpp"
#include "support/Program.hpp"

#include "pgm/cart/Bios.hpp"
#include "pgm/control/Dispatcher.hpp"
#include "pgm/machine/Machine.hpp"

#include <algorithm>
#include <array>
#include <string>

using pgm::control::Json;
using pgm::machine::Machine;
using pgm::machine::StopReason;
using pgm::test::high;
using pgm::test::low;
using pgm::test::valueOf;

namespace
{

constexpr std::uint32_t STACK = 0x820000;
constexpr std::uint32_t ENTRY = 0x400;
constexpr std::uint32_t LOOP = 0x40e;
constexpr std::uint32_t VBLANK_HANDLER = 0x500;
constexpr std::uint32_t COUNTER = 0x800000;
constexpr std::uint32_t FLAGS = 0xb0e000;

// Interrupt 6 is autovectored: vector 24 + 6.
constexpr std::uint32_t LEVEL_6_AUTOVECTOR = 30;

/// A BIOS program that counts vertical blanks: it enables interrupt 6 and
/// waits, and the handler acknowledges the interrupt by clearing and setting
/// its enable, as PGM software does, and adds one to a word of work RAM.
pgm::test::Program vblankCounter()
{
  pgm::test::Program program{ STACK, ENTRY };
  program.vector( LEVEL_6_AUTOVECTOR, VBLANK_HANDLER );
  program.at( ENTRY,
              {
                  0x46fc,
                  0x2000, // move.w #$2000,sr
                  0x33fc,
                  0x0008,
                  high( FLAGS ),
                  low( FLAGS ), // move.w #8,$b0e000
                  0x4e71,       // nop
                  0x60fe,       // loop: bra.s loop
              } );
  program.at( VBLANK_HANDLER,
              {
                  0x33fc,
                  0x0000,
                  high( FLAGS ),
                  low( FLAGS ), // move.w #0,$b0e000
                  0x33fc,
                  0x0008,
                  high( FLAGS ),
                  low( FLAGS ), // move.w #8,$b0e000
                  0x5279,
                  high( COUNTER ),
                  low( COUNTER ), // addq.w #1,$800000
                  0x4e73,         // rte
              } );
  return program;
}

/// A BIOS made of `program` and empty tiles and samples, read from a scratch
/// directory as the emulator reads one.
class BiosFixture
{
public:
  explicit BiosFixture( pgm::test::Program const& program )
  {
    pgm::test::writeFile( mScratch.path() / "pgm_p02s.u20", program.bytes() );
    pgm::test::writeFile( mScratch.path() / "pgm_t01s.rom", std::vector<std::uint8_t>( 0x200000, 0 ) );
    pgm::test::writeFile( mScratch.path() / "pgm_m01s.rom", std::vector<std::uint8_t>( 0x200000, 0 ) );
  }

  [[nodiscard]] std::filesystem::path const& directory() const
  {
    return mScratch.path();
  }

  [[nodiscard]] pgm::cart::Bios load() const
  {
    std::array const places{ mScratch.path() };
    return valueOf( pgm::cart::Bios::load( valueOf( pgm::io::RomSources::open( places ) ) ) );
  }

private:
  pgm::test::TemporaryDirectory mScratch;
};

std::uint16_t wordAt( std::span<std::uint8_t const> ram, std::size_t at )
{
  return static_cast<std::uint16_t>( ( ram[at] << 8U ) | ram[at + 1] );
}

} // namespace

TEST_CASE( "the 68000 runs the BIOS and takes the vertical blank interrupt once a frame", "[machine]" )
{
  BiosFixture const fixture{ vblankCounter() };
  auto const bios = fixture.load();
  Machine machine{ bios, nullptr };
  machine.reset( 100 );

  auto const run = machine.runFrames( 5 );

  REQUIRE( run.reason == StopReason::COMPLETED );
  REQUIRE( run.frames == 5 );
  // A frame ends 11 master ticks after its interrupt is raised, too soon for
  // the 68000 to have taken it: five frames hold four handled interrupts.
  REQUIRE( wordAt( machine.workRam(), COUNTER & 0x1ffffU ) == 4 );
  REQUIRE( machine.m68kState().pc >= LOOP );
}

TEST_CASE( "frames are counted where the RTL simulator counts them", "[machine]" )
{
  BiosFixture const fixture{ vblankCounter() };
  auto const bios = fixture.load();
  Machine machine{ bios, nullptr };
  machine.reset( 100 );

  auto const run = machine.runFrames( 60 );

  // The simulator's sim.run_frames 60 after sim.reset 100 takes 50687911
  // master ticks; a run ends between instructions, a few ticks later.
  REQUIRE( run.ticks >= 50687911 );
  REQUIRE( run.ticks < 50687911 + 100 );
}

TEST_CASE( "a run stops at a breakpoint before the instruction, and the next run executes it", "[machine]" )
{
  BiosFixture const fixture{ vblankCounter() };
  auto const bios = fixture.load();
  Machine machine{ bios, nullptr };
  machine.addBreakpoint( VBLANK_HANDLER );

  auto const first = machine.runFrames( 3 );
  REQUIRE( first.reason == StopReason::BREAKPOINT );
  REQUIRE( machine.m68kState().pc == VBLANK_HANDLER );

  auto const second = machine.runTicks( 1000 );
  REQUIRE( second.reason == StopReason::COMPLETED );
  REQUIRE( wordAt( machine.workRam(), COUNTER & 0x1ffffU ) == 1 );
}

TEST_CASE( "run_until stops where its condition first holds", "[machine][control]" )
{
  BiosFixture const fixture{ vblankCounter() };
  pgm::Emulator emulator{ pgm::Settings{
      .biosSources = { fixture.directory() }, .romDirectory = {}, .stateDirectory = {} } };
  pgm::control::Dispatcher const dispatcher{ emulator };
  auto const call = [&]( std::string const& method, Json params )
  { return dispatcher.handle( Json{ { "id", 1 }, { "method", method }, { "params", std::move( params ) } } ); };
  REQUIRE( call( "sim.load_game", { { "name", "pgm" } } ).at( "ok" ) == true );

  auto const run = call( "sim.run_until",
                         { { "condition", { { "type", "cpu_pc_equals" }, { "value", VBLANK_HANDLER } } },
                           { "timeout_cycles", 2000000 } } );

  REQUIRE( run.at( "result" ).at( "reason" ) == "condition_met" );
  REQUIRE( call( "cpu.get_state", Json::object() ).at( "result" ).at( "pc" ) == VBLANK_HANDLER );
}

TEST_CASE( "a run stops after the instruction that writes a watched address", "[machine]" )
{
  BiosFixture const fixture{ vblankCounter() };
  auto const bios = fixture.load();
  Machine machine{ bios, nullptr };
  machine.addWatchpoint( pgm::machine::Watchpoint{ .address = COUNTER + 1, .size = 1, .read = false, .write = true } );

  auto const run = machine.runFrames( 3 );

  REQUIRE( run.reason == StopReason::WATCHPOINT );
  auto const hit = valueOf( machine.watchpointHit() );
  REQUIRE( hit.write );
  REQUIRE( hit.address == COUNTER );
  REQUIRE( hit.value == 1 );
  REQUIRE( hit.pc == VBLANK_HANDLER + 16 );
  REQUIRE( wordAt( machine.workRam(), COUNTER & 0x1ffffU ) == 1 );

  // The addq reads the counter before it writes it; a read watchpoint sees the
  // read, and not the fetch of the instruction words around it.
  machine.removeWatchpoint( COUNTER + 1 );
  machine.addWatchpoint( pgm::machine::Watchpoint{ .address = COUNTER, .size = 2, .read = true, .write = false } );
  REQUIRE( machine.runFrames( 3 ).reason == StopReason::WATCHPOINT );
  auto const read = valueOf( machine.watchpointHit() );
  REQUIRE_FALSE( read.write );
  REQUIRE( read.value == 1 );

  machine.removeWatchpoint( COUNTER );
  REQUIRE( machine.watchpoints().empty() );
  REQUIRE( machine.runFrames( 3 ).reason == StopReason::COMPLETED );
}

TEST_CASE( "the trace holds the last instructions executed, oldest first", "[machine]" )
{
  BiosFixture const fixture{ vblankCounter() };
  auto const bios = fixture.load();
  Machine machine{ bios, nullptr };
  machine.addBreakpoint( VBLANK_HANDLER + 16 );
  machine.runFrames( 3 );

  auto const trace = machine.trace( 3 );

  REQUIRE( trace.size() == 3 );
  // The loop, then the handler up to the breakpoint.
  REQUIRE( trace[0].pc == LOOP );
  REQUIRE( trace[1].pc == VBLANK_HANDLER );
  REQUIRE( trace[2].pc == VBLANK_HANDLER + 8 );
  REQUIRE( trace[0].ticks < trace[1].ticks );
  REQUIRE( machine.trace( 100000 ).size() <= Machine::TRACE_SIZE );
}

// The test BIOS leaves the Z80 out of reset with its RAM clear, so it runs
// NOPs through all of it: every Z80 address is an instruction it reaches.
TEST_CASE( "a Z80 breakpoint ends the run, with the Z80's registers as they were there", "[machine]" )
{
  BiosFixture const fixture{ vblankCounter() };
  auto const bios = fixture.load();
  Machine machine{ bios, nullptr };
  REQUIRE( machine.addBreakpoint( 0x100, pgm::machine::Cpu::Z80 ) );
  REQUIRE( machine.breakpoints( pgm::machine::Cpu::Z80 ).contains( 0x100 ) );
  REQUIRE( machine.hasDebugPoints() );

  // The first frame boundary comes 11 master ticks after power-up.
  auto const run = machine.runFrames( 2 );

  REQUIRE( run.reason == StopReason::BREAKPOINT );
  auto const hit = valueOf( machine.breakpointHit() );
  REQUIRE( hit.cpu == pgm::machine::Cpu::Z80 );
  REQUIRE( hit.address == 0x100 );
  REQUIRE( valueOf( hit.z80 ).pc == 0x100 );
  // The Z80 itself ran on to the 68000's time, a few NOPs further.
  REQUIRE( machine.z80Registers().pc > 0x100 );

  machine.removeBreakpoint( 0x100, pgm::machine::Cpu::Z80 );
  REQUIRE_FALSE( machine.hasDebugPoints() );
  REQUIRE( machine.runFrames( 3 ).reason == StopReason::COMPLETED );
  REQUIRE_FALSE( machine.breakpointHit().has_value() );
}

// Compared by what a machine shows rather than by its save state, whose
// bytes include structs' padding, which no run sets.
TEST_CASE( "stopping at Z80 breakpoints leaves the emulation as it would have been", "[machine]" )
{
  BiosFixture const fixture{ vblankCounter() };
  auto const bios = fixture.load();
  Machine plain{ bios, nullptr };
  Machine stopped{ bios, nullptr };
  stopped.addBreakpoint( 0x2000, pgm::machine::Cpu::Z80 );

  plain.runFrames( 4 );
  int stops = 0;
  while ( stopped.frame() < 4 )
  {
    if ( stopped.runFrames( 1 ).reason == StopReason::BREAKPOINT )
    {
      ++stops;
    }
  }

  REQUIRE( stops > 0 );
  REQUIRE( stopped.now() == plain.now() );
  REQUIRE( std::ranges::equal( stopped.workRam(), plain.workRam() ) );
  REQUIRE( std::ranges::equal( stopped.videoRam(), plain.videoRam() ) );
  REQUIRE( std::ranges::equal( stopped.paletteRam(), plain.paletteRam() ) );
  REQUIRE( std::ranges::equal( stopped.z80Ram(), plain.z80Ram() ) );
  REQUIRE( std::ranges::equal( stopped.picture(), plain.picture() ) );
  REQUIRE( stopped.m68kState().pc == plain.m68kState().pc );
  REQUIRE( stopped.m68kState().d == plain.m68kState().d );
  REQUIRE( stopped.m68kState().a == plain.m68kState().a );
  REQUIRE( stopped.m68kState().sr == plain.m68kState().sr );
  REQUIRE( stopped.z80Registers().pc == plain.z80Registers().pc );
  REQUIRE( stopped.z80Registers().af == plain.z80Registers().af );
  REQUIRE( stopped.z80Registers().sp == plain.z80Registers().sp );
}

TEST_CASE( "the board without an IGS027A takes no ARM7 breakpoint", "[machine]" )
{
  BiosFixture const fixture{ vblankCounter() };
  auto const bios = fixture.load();
  Machine machine{ bios, nullptr };

  REQUIRE_FALSE( machine.addBreakpoint( 0x1000, pgm::machine::Cpu::ARM7 ) );
  REQUIRE( machine.breakpoints( pgm::machine::Cpu::ARM7 ).empty() );
}

TEST_CASE( "a debugger writes RAM whole or not at all", "[machine]" )
{
  BiosFixture const fixture{ vblankCounter() };
  auto const bios = fixture.load();
  Machine machine{ bios, nullptr };
  std::array<std::uint8_t, 2> const bytes{ 0x12, 0x34 };

  REQUIRE( machine.writeRam( pgm::machine::Ram::WORK, 0x100, bytes ) );
  REQUIRE( wordAt( machine.workRam(), 0x100 ) == 0x1234 );
  REQUIRE( machine.writeRam( pgm::machine::Ram::AUDIO, 0xfffe, bytes ) );
  REQUIRE( wordAt( machine.z80Ram(), 0xfffe ) == 0x1234 );
  REQUIRE( machine.writeRam( pgm::machine::Ram::PALETTE, 0, bytes ) );
  REQUIRE( wordAt( machine.paletteRam(), 0 ) == 0x1234 );

  REQUIRE_FALSE( machine.writeRam( pgm::machine::Ram::VIDEO, machine.videoRam().size() - 1, bytes ) );
  REQUIRE( machine.videoRam().back() == 0 );
}
