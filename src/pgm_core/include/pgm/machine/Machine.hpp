#pragma once

#include "pgm/cart/Bios.hpp"
#include "pgm/cart/PgmImage.hpp"
#include "pgm/machine/Sound.hpp"
#include "pgm/machine/Time.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace pgm::machine
{

/// The 68000's registers, as the debugger and `cpu.get_state` show them.
struct M68kState
{
  std::array<std::uint32_t, 8> d{};
  std::array<std::uint32_t, 8> a{};
  std::uint32_t pc{};
  std::uint16_t sr{};
  std::uint32_t usp{};
  std::uint32_t ssp{};
  bool stopped{};
  bool halted{};
};

/// Why a run returned.
enum class StopReason : std::uint8_t
{
  /// It ran as long as it was asked to.
  COMPLETED,
  /// The 68000 reached a breakpoint; it has not executed that instruction.
  BREAKPOINT,
  /// The 68000 read or wrote a watched address; the instruction that did it
  /// has executed.
  WATCHPOINT,
  /// The condition it was given became true.
  CONDITION_MET,
  /// The condition did not become true in the time allowed.
  TIMEOUT,
  /// The 68000 halted (a double bus fault) and cannot go on.
  HALTED
};

/// A range of the 68000's address space whose data reads or writes stop a run.
struct Watchpoint
{
  std::uint32_t address{};
  std::uint32_t size{ 1 };
  bool read{};
  bool write{ true };
};

/// The access that stopped a run at a watchpoint.
struct WatchpointHit
{
  std::uint32_t address{};
  bool write{};
  /// What was read or written: a byte or a word.
  std::uint16_t value{};
  std::uint8_t bytes{};
  /// The instruction that made the access.
  std::uint32_t pc{};
};

/// An instruction the 68000 executed, and when it began.
struct TraceEntry
{
  std::uint32_t pc{};
  std::int64_t ticks{};
};

struct RunResult
{
  StopReason reason{};
  /// Master ticks (50 MHz) the run took.
  std::int64_t ticks{};
  /// Frame boundaries it crossed.
  std::int64_t frames{};
};

/// The PGM board: the 68000 and every device it reaches, running in emulated
/// time from power-up. docs/architecture.md describes how time is kept.
class Machine
{
public:
  /// Powers the board up with `bios` and, unless it is null, `cartridge`
  /// inserted. Its reset line is held for the first 100 master ticks, as the
  /// RTL simulator's front end holds it; a reset() before the machine runs
  /// takes their place. Both must outlive the machine.
  Machine( cart::Bios const& bios, cart::PgmImage const* cartridge );
  ~Machine();

  Machine( Machine const& ) = delete;
  Machine& operator=( Machine const& ) = delete;
  Machine( Machine&& ) = delete;
  Machine& operator=( Machine&& ) = delete;

  /// Holds the reset line for `masterTicks` from now, running the raster on
  /// through it as the board does, then lets it go: the 68000 fetches its
  /// vectors as soon as the machine runs. This is the RTL simulator's
  /// `sim.reset`, which also returns with the time spent.
  void reset( std::int64_t masterTicks );

  /// Runs until `frames` frame boundaries have passed. A boundary is where the
  /// RTL simulator's `vblank` output rises: 11 master ticks after line 0 begins.
  RunResult runFrames( std::int64_t frames );

  /// Runs for at least `masterTicks`; it stops between instructions.
  RunResult runTicks( std::int64_t masterTicks );

  /// Runs until `condition` holds after an instruction, or `timeoutTicks` pass.
  RunResult runUntil( std::function<bool()> const& condition, std::int64_t timeoutTicks );

  [[nodiscard]] Time now() const;
  /// Frame boundaries passed since power-up.
  [[nodiscard]] std::int64_t frame() const;
  [[nodiscard]] int line() const;
  [[nodiscard]] int dot() const;
  [[nodiscard]] bool vblank() const;
  [[nodiscard]] bool hblank() const;

  [[nodiscard]] M68kState m68kState() const;
  /// The Z80's registers, as of the last time the 68000 or the end of a run
  /// caught the sound side up.
  [[nodiscard]] Z80Registers z80Registers() const;
  [[nodiscard]] bool z80Halted() const;
  /// The instruction at `address`, and its length in bytes.
  [[nodiscard]] std::string disassemble( std::uint32_t address, int& length ) const;
  /// The word at `address` as the 68000 would read it from ROM or work RAM, or
  /// 0 elsewhere; it clocks no device.
  [[nodiscard]] std::uint16_t peek( std::uint32_t address ) const;

  /// The whole board's state, as a save state holds it: everything a run
  /// changes, and nothing of the ROMs, the breakpoints or the listeners.
  [[nodiscard]] std::vector<std::uint8_t> saveState();
  /// Restores what saveState() wrote. A machine with other ROMs takes it but
  /// runs on wrongly; checking that is the caller's. False, and the machine
  /// unchanged, when `state` is not one this build wrote.
  bool loadState( std::span<std::uint8_t const> state );

  /// Instruction addresses a run stops at, before the instruction executes.
  void addBreakpoint( std::uint32_t address );
  void removeBreakpoint( std::uint32_t address );
  [[nodiscard]] std::set<std::uint32_t> const& breakpoints() const;

  /// Data accesses of the 68000 that stop a run, after the instruction that
  /// made them. Instruction fetches do not count. One watchpoint per address;
  /// adding another at the same address replaces it.
  void addWatchpoint( Watchpoint watchpoint );
  void removeWatchpoint( std::uint32_t address );
  [[nodiscard]] std::vector<Watchpoint> const& watchpoints() const;
  /// The access that ended the last run at a watchpoint.
  [[nodiscard]] std::optional<WatchpointHit> const& watchpointHit() const;

  /// The last `count` instructions the 68000 executed, oldest first; at most
  /// TRACE_SIZE are kept.
  static constexpr std::size_t TRACE_SIZE = 4096;
  [[nodiscard]] std::vector<TraceEntry> trace( std::size_t count ) const;

  /// The four input words of PGM.sv, IN0..IN3, with a set bit for a pressed
  /// button; IN3's low byte is the DIP switches.
  void setInputs( std::array<std::uint16_t, 4> const& pressed );

  [[nodiscard]] std::span<std::uint8_t const> workRam() const;
  /// Replaces the work RAM, which the board keeps powered by its battery: what
  /// `nvram.load` does. `bytes` is 128 KB in 68000 byte order.
  void setWorkRam( std::span<std::uint8_t const> bytes );
  [[nodiscard]] std::span<std::uint8_t const> videoRam() const;
  [[nodiscard]] std::span<std::uint8_t const> paletteRam() const;
  [[nodiscard]] std::span<std::uint8_t const> z80Ram() const;

  /// The last complete frame: 448 by 224 pixels, RGBA, row by row. It is
  /// complete when vertical blank begins, which is just before a frame boundary.
  [[nodiscard]] std::span<std::uint8_t const> picture() const;
  /// Pictures completed since power-up.
  [[nodiscard]] std::int64_t picturesDrawn() const;

  /// The ICS2115's output during the last run, at its own rate: a frame per
  /// 32 of its clocks per active voice, about 33 kHz with all 32.
  [[nodiscard]] std::span<AudioFrame const> audio() const;
  /// The rate audio() comes at now, in frames per second.
  [[nodiscard]] double audioRate() const;
  /// Called with audio() as every run ends; an empty function stops the calls.
  void setAudioListener( std::function<void( std::span<AudioFrame const> )> listener );
  [[nodiscard]] std::array<Ics2115Voice, 32> const& ics2115Voices() const;
  [[nodiscard]] std::size_t ics2115ActiveVoices() const;

private:
  struct Parts;
  std::unique_ptr<Parts> mParts;
};

} // namespace pgm::machine
