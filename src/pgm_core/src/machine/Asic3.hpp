#pragma once

// The ASIC3 protection of Oriental Legend, ported from rtl/pgm_asic3.sv. The
// 68000 reaches it at 0xC04000-0xC0400F on every board, as PGM.sv decodes it
// whatever the cartridge: the first word selects a register, the others write
// it or read it back.

#include <array>
#include <cstdint>

namespace pgm::machine
{

class Asic3
{
public:
  /// `region` is the value the game reads as its region: 0 or 1 is the world.
  explicit Asic3( std::uint8_t region );

  void reset();
  [[nodiscard]] std::uint16_t read() const;
  /// A bus write at byte address `address`, whatever its strobes.
  void write( std::uint32_t address, std::uint16_t value );

private:
  [[nodiscard]] std::uint16_t nextHold( std::uint16_t data ) const;

  std::uint8_t mRegion;
  std::uint8_t mRegister{};
  std::array<std::uint8_t, 3> mLatch{};
  std::uint8_t mX{};
  std::uint16_t mHilo{};
  std::uint16_t mHold{};
};

} // namespace pgm::machine
