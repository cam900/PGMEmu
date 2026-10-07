#pragma once

// The IGS023 video chip as the 68000 sees it, ported from rtl/igs023.sv: its
// registers, VRAM, palette RAM, raster timing, line counter and interrupts.
// What it draws is added in M3.

#include "pgm/machine/Time.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace pgm::machine
{

class Igs023
{
public:
  static constexpr std::size_t VRAM_SIZE = 0x8000;
  static constexpr std::size_t PALETTE_SIZE = 0x2000;

  /// What the reset line does to the chip: drops both interrupts. Registers,
  /// RAM and the raster are untouched, as igs023.sv leaves them.
  void reset();

  /// Brings the raster up to `now`: the line counter, sprite DMA's trigger
  /// and the interrupts it raises on the way.
  void advanceTo( Time now );

  /// Whether interrupt 6 (vertical blank) and 4 (every 62 lines) are raised.
  [[nodiscard]] bool irq6() const;
  [[nodiscard]] bool irq4() const;

  /// A 68000 word read or write at byte address `address` in 0x900000-0xBFFFFF.
  /// `upper` and `lower` are the data strobes: which bytes take part.
  std::uint16_t read( Time now, std::uint32_t address, bool upper, bool lower );
  void write( Time now, std::uint32_t address, std::uint16_t value, bool upper, bool lower );

  /// The 68000 cycles a bus cycle to the chip waits for its DTACK.
  ///
  /// VRAM is 8 bits wide, and igs023.sv moves a word through it a byte at a
  /// time, two 50 MHz clocks a byte, before it acknowledges: DTACK comes about
  /// six master ticks after the chip select for a word and four for a byte.
  /// Registers and palette RAM acknowledge on the clock after it. The 68000
  /// samples DTACK at the end of S4, and asserts its data strobes at S2 for a
  /// read but S4 for a write, so a write waits longer for the same delay.
  /// Contention with the layers' own VRAM fetches, which the RTL arbitrates,
  /// is not counted until the layers are emulated (M3).
  [[nodiscard]] static int waitStates( std::uint32_t address, bool write, bool upper, bool lower );

  /// VRAM as the chip's 8-bit RAM holds it, and palette RAM in the 68000's
  /// byte order: what the RTL simulator's VIDEO_RAM and PALETTE_RAM are.
  [[nodiscard]] std::span<std::uint8_t const> vram() const;
  [[nodiscard]] std::span<std::uint8_t const> palette() const;

  /// Raster position at `now`, for conditions and the debugger.
  [[nodiscard]] static int line( Time now );
  [[nodiscard]] static int dot( Time now );
  [[nodiscard]] static bool vblank( Time now );
  [[nodiscard]] static bool hblank( Time now );

private:
  void onLineStart( int line );
  void onHsync();
  [[nodiscard]] std::uint16_t controlFlags() const;

  std::array<std::uint16_t, 16> mControl{};
  std::array<std::uint16_t, 32> mZoomTable{};
  std::array<std::uint8_t, VRAM_SIZE> mVram{};
  std::array<std::uint8_t, PALETTE_SIZE> mPalette{};
  bool mIrq6{};
  bool mIrq4{};
  std::uint8_t mIrq4Count{};
  /// The raster events up to here have been applied: each line has two, the
  /// start of its first dot and the rising edge of its hsync.
  std::int64_t mEventsDone{};
};

} // namespace pgm::machine
