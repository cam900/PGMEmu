#pragma once

// A cartridge's protection chips, as the 68000 reaches them. Which chips a
// cartridge has, and where they answer, is address_translator.sv's per-game
// decode at MiSTer core commit e898860; every one of them takes addresses the
// board's own decode would give to ROM or leave open. ASIC3 is not one of
// them: PGM.sv decodes it on every board (Asic3.hpp).

#include "StateArchive.hpp"

#include "pgm/machine/Time.hpp"

#include <cstdint>
#include <vector>

namespace pgm::machine
{

class Igs027a;

class Protection
{
public:
  Protection() = default;
  virtual ~Protection() = default;

  Protection( Protection const& ) = delete;
  Protection& operator=( Protection const& ) = delete;
  Protection( Protection&& ) = delete;
  Protection& operator=( Protection&& ) = delete;

  /// The 64 KB pages of the 68000's space, by `address >> 16`, that hold an
  /// address of the chips: the bus asks decodes() only there.
  [[nodiscard]] virtual std::vector<std::uint8_t> pages() const = 0;

  /// Whether the chips answer at byte address `address`.
  [[nodiscard]] virtual bool decodes( std::uint32_t address ) const = 0;

  /// A bus cycle at `time`, as Bus68k makes them. A chip that keeps the 68000
  /// waiting adds the wait to `time`.
  virtual std::uint16_t read( Time& time, std::uint32_t address, bool upper, bool lower ) = 0;
  virtual void write( Time& time, std::uint32_t address, std::uint16_t value, bool upper, bool lower ) = 0;

  /// A read for the debugger: no chip is clocked by it.
  [[nodiscard]] virtual std::uint16_t peek( std::uint32_t address ) const = 0;

  /// The board's reset, its line let go at `releasedAt`.
  virtual void reset( Time releasedAt ) = 0;

  /// Brings chips that run on their own clock up to `now`.
  virtual void advanceTo( Time /*now*/ ) {}

  /// The board's IGS027A, for the debugger; null on boards without one.
  [[nodiscard]] virtual Igs027a const* igs027a() const
  {
    return nullptr;
  }

  /// Names its state for a save state (StateArchive.hpp), in each direction.
  virtual void serialize( StateWriter& archive ) = 0;
  virtual void serialize( StateReader& archive ) = 0;
};

} // namespace pgm::machine
