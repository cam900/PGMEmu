#pragma once

// The IGS026 glue between the 68000, the Z80 and the RTC, ported from
// rtl/igs026_x.sv: the sound latches, the Z80's reset and bus request, the
// 68000's window onto the Z80's 64 KB of RAM, and the RTC's serial port.
//
// Until the Z80 is emulated (M4) it is taken to grant the bus as soon as it is
// asked for, as a running Z80 does within a few of its cycles.

#include "V3021.hpp"

#include "pgm/machine/Time.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace pgm::machine
{

class Igs026
{
public:
  static constexpr std::size_t Z80_RAM_SIZE = 0x10000;

  /// What the reset line does: clears the latches and the Z80's NMI. The Z80's
  /// RAM and the RTC keep their contents.
  void reset();

  /// A 68000 word read or write at byte address `address` in 0xC00000-0xC1FFFF.
  std::uint16_t read( Time now, std::uint32_t address, bool upper, bool lower );
  void write( Time now, std::uint32_t address, std::uint16_t value, bool upper, bool lower );

  /// The Z80's RAM, by Z80 address: what the RTL simulator's AUDIO_RAM is.
  [[nodiscard]] std::span<std::uint8_t const> z80Ram() const;

  /// Whether the Z80 is held in reset by the 68000 (register 0xC00008 bit 0).
  [[nodiscard]] bool z80InReset() const;

private:
  /// Whether the 68000 owns the Z80's RAM: it has asked for the bus (0x45D3 in
  /// 0xC0000A) and the Z80 is in reset or has granted it.
  [[nodiscard]] bool z80BusGranted() const;

  // latch[1], [2], [4], [5], [6] of the RTL, at 0xC00002 to 0xC0000C.
  std::array<std::uint16_t, 8> mLatch{};
  std::array<std::uint8_t, Z80_RAM_SIZE> mZ80Ram{};
  bool mZ80Nmi{};
  V3021 mRtc;
};

} // namespace pgm::machine
