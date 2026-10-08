#pragma once

#include "pgm/cart/Bios.hpp"
#include "pgm/cart/PgmImage.hpp"
#include "pgm/machine/Time.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <span>
#include <string>

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
  /// The condition it was given became true.
  CONDITION_MET,
  /// The condition did not become true in the time allowed.
  TIMEOUT,
  /// The 68000 halted (a double bus fault) and cannot go on.
  HALTED
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
  /// The instruction at `address`, and its length in bytes.
  [[nodiscard]] std::string disassemble( std::uint32_t address, int& length ) const;
  /// The word at `address` as the 68000 would read it from ROM or work RAM, or
  /// 0 elsewhere; it clocks no device.
  [[nodiscard]] std::uint16_t peek( std::uint32_t address ) const;

  /// Instruction addresses a run stops at, before the instruction executes.
  void addBreakpoint( std::uint32_t address );
  void removeBreakpoint( std::uint32_t address );
  [[nodiscard]] std::set<std::uint32_t> const& breakpoints() const;

  /// The four input words of PGM.sv, IN0..IN3, with a set bit for a pressed
  /// button; IN3's low byte is the DIP switches.
  void setInputs( std::array<std::uint16_t, 4> const& pressed );

  [[nodiscard]] std::span<std::uint8_t const> workRam() const;
  [[nodiscard]] std::span<std::uint8_t const> videoRam() const;
  [[nodiscard]] std::span<std::uint8_t const> paletteRam() const;
  [[nodiscard]] std::span<std::uint8_t const> z80Ram() const;

  /// The last complete frame: 448 by 224 pixels, RGBA, row by row. It is
  /// complete when vertical blank begins, which is just before a frame boundary.
  [[nodiscard]] std::span<std::uint8_t const> picture() const;
  /// Pictures completed since power-up.
  [[nodiscard]] std::int64_t picturesDrawn() const;

private:
  struct Parts;
  std::unique_ptr<Parts> mParts;
};

} // namespace pgm::machine
