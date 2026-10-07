#pragma once

// The 68000's address space: the chip selects of rtl/address_translator.sv and
// the order PGM.sv's data multiplexer gives them. Protection devices are added
// to it per board in M7.

#include "Igs023.hpp"
#include "Igs026.hpp"

#include "pgm/machine/Time.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace pgm::machine
{

/// The 68000's ROM space, 0x000000-0x7FFFFF, as the RTL fetches it from SDRAM.
///
/// Below the cartridge's program base the RTL reads the BIOS's SDRAM region,
/// which holds the 128 KB program, then zeros, then the BIOS tiles at 1 MB and
/// samples at 3 MB. Without a cartridge that is the whole space, so a probe of
/// 0x100000 finds the BIOS's own tiles there (docs/hardware/).
struct RomSpace
{
  std::span<std::uint8_t const> biosProgram;
  std::span<std::uint8_t const> biosTiles;
  std::span<std::uint8_t const> biosMusic;
  /// Empty when no cartridge is inserted.
  std::span<std::uint8_t const> cartProgram;
  std::uint32_t cartBase{};

  /// The 68000 word at even byte address `address`. ROM files hold each word
  /// with its low byte first.
  [[nodiscard]] std::uint16_t word( std::uint32_t address ) const;
};

/// Inputs as the board's I/O ports read them, before inversion: a set bit is a
/// pressed button. The layout of each word is IN0..IN3 of PGM.sv.
struct InputPorts
{
  std::array<std::uint16_t, 4> pressed{};
};

class Bus68k
{
public:
  /// `time` is the machine's clock: the time of every bus cycle, and what a
  /// device that keeps the 68000 waiting adds its wait states to.
  Bus68k( RomSpace rom, Igs023& video, Igs026& io, InputPorts const& inputs, Time& time );

  /// A bus cycle now. Byte accesses are word cycles with one strobe; the
  /// written byte is on both halves of `value`, as the 68000 drives it.
  std::uint16_t read( std::uint32_t address, bool upper, bool lower );
  void write( std::uint32_t address, std::uint16_t value, bool upper, bool lower );

  /// A read for the debugger and the disassembler: no device is clocked by it.
  [[nodiscard]] std::uint16_t peek( std::uint32_t address ) const;

  [[nodiscard]] std::span<std::uint8_t const> workRam() const;

private:
  RomSpace mRom;
  Igs023& mVideo;
  Igs026& mIo;
  InputPorts const& mInputs;
  Time& mTime;
  /// 128 KB of work RAM in the 68000's byte order.
  std::array<std::uint8_t, 0x20000> mWorkRam{};
};

} // namespace pgm::machine
