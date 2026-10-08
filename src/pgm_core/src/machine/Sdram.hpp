#pragma once

// The MiSTer core's SDRAM, as its devices read it: every ROM at the place
// system_consts.sv gives it, and zero wherever nothing was loaded. The 68000's
// ROM space and the IGS023's tile fetches both reach past the ROMs they mean to
// read, and what they find there is this layout (docs/hardware/differences.md).

#include <cstdint>
#include <span>

namespace pgm::machine
{

struct Sdram
{
  // system_consts.sv
  static constexpr std::uint32_t BIOS_PROGRAM_AT = 0x000000;
  static constexpr std::uint32_t BIOS_TILES_AT = 0x100000;
  static constexpr std::uint32_t BIOS_MUSIC_AT = 0x300000;
  static constexpr std::uint32_t CART_PROGRAM_AT = 0x0800000;
  static constexpr std::uint32_t CART_TILES_AT = 0x1000000;
  static constexpr std::uint32_t CART_MUSIC_AT = 0x2000000;
  static constexpr std::uint32_t CART_B_ROM_AT = 0x3000000;
  static constexpr std::uint32_t CART_A_ROM_AT = 0x4000000;

  std::span<std::uint8_t const> biosProgram;
  std::span<std::uint8_t const> biosTiles;
  std::span<std::uint8_t const> biosMusic;
  std::span<std::uint8_t const> cartProgram;
  std::span<std::uint8_t const> cartTiles;
  std::span<std::uint8_t const> cartMusic;
  std::span<std::uint8_t const> cartBRom;
  std::span<std::uint8_t const> cartARom;

  /// The byte at `address`.
  [[nodiscard]] std::uint8_t byte( std::uint32_t address ) const;

  /// The 16-bit word at even `address`, its low byte first as the ROM files
  /// hold it.
  [[nodiscard]] std::uint16_t word( std::uint32_t address ) const;

  /// The 32-bit word at `address`, lowest byte first.
  [[nodiscard]] std::uint32_t longWord( std::uint32_t address ) const;
};

} // namespace pgm::machine
