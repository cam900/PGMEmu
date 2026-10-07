#pragma once

#include <cstdint>
#include <initializer_list>
#include <vector>

namespace pgm::test
{

/// A BIOS program for the 68000, written word by word, as a 128 KB ROM file
/// holds it: every word with its low byte first.
class Program
{
public:
  /// Starts a program whose reset vectors give `stack` and `entry`.
  Program( std::uint32_t stack, std::uint32_t entry );

  /// Places `words` from `address` on.
  Program& at( std::uint32_t address, std::initializer_list<std::uint16_t> words );

  /// Points exception vector `number` at `handler`.
  Program& vector( std::uint32_t number, std::uint32_t handler );

  [[nodiscard]] std::vector<std::uint8_t> const& bytes() const;

private:
  void putWord( std::uint32_t address, std::uint16_t word );

  std::vector<std::uint8_t> mBytes;
};

/// The high and low word of a 32-bit operand, as an instruction stream holds it.
constexpr std::uint16_t high( std::uint32_t value )
{
  return static_cast<std::uint16_t>( value >> 16U );
}

constexpr std::uint16_t low( std::uint32_t value )
{
  return static_cast<std::uint16_t>( value );
}

} // namespace pgm::test
