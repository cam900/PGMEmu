#include "Sdram.hpp"

#include <array>

namespace pgm::machine
{

namespace
{

/// The bytes of the region holding `address`, from `address` on; empty where
/// nothing was loaded.
std::span<std::uint8_t const> from( Sdram const& sdram, std::uint32_t address )
{
  struct Region
  {
    std::uint32_t at;
    std::span<std::uint8_t const> bytes;
  };

  // Highest first: every region starts above the end of the one before.
  std::array const regions{ Region{ .at = Sdram::CART_A_ROM_AT, .bytes = sdram.cartARom },
                            Region{ .at = Sdram::CART_B_ROM_AT, .bytes = sdram.cartBRom },
                            Region{ .at = Sdram::CART_MUSIC_AT, .bytes = sdram.cartMusic },
                            Region{ .at = Sdram::CART_TILES_AT, .bytes = sdram.cartTiles },
                            Region{ .at = Sdram::CART_PROGRAM_AT, .bytes = sdram.cartProgram },
                            Region{ .at = Sdram::BIOS_MUSIC_AT, .bytes = sdram.biosMusic },
                            Region{ .at = Sdram::BIOS_TILES_AT, .bytes = sdram.biosTiles },
                            Region{ .at = Sdram::BIOS_PROGRAM_AT, .bytes = sdram.biosProgram } };
  for ( Region const& region : regions )
  {
    if ( address >= region.at )
    {
      std::uint32_t const offset = address - region.at;
      return offset < region.bytes.size() ? region.bytes.subspan( offset ) : std::span<std::uint8_t const>{};
    }
  }
  return {};
}

} // namespace

std::uint8_t Sdram::byte( std::uint32_t address ) const
{
  auto const bytes = from( *this, address );
  return bytes.empty() ? 0 : bytes[0];
}

std::uint16_t Sdram::word( std::uint32_t address ) const
{
  auto const bytes = from( *this, address );
  if ( bytes.size() >= 2 )
  {
    return static_cast<std::uint16_t>( bytes[0] | ( bytes[1] << 8U ) );
  }
  return static_cast<std::uint16_t>( byte( address ) | ( byte( address + 1 ) << 8U ) );
}

std::uint32_t Sdram::longWord( std::uint32_t address ) const
{
  auto const bytes = from( *this, address );
  if ( bytes.size() >= 4 )
  {
    return static_cast<std::uint32_t>( bytes[0] ) | ( static_cast<std::uint32_t>( bytes[1] ) << 8U ) |
           ( static_cast<std::uint32_t>( bytes[2] ) << 16U ) | ( static_cast<std::uint32_t>( bytes[3] ) << 24U );
  }
  return static_cast<std::uint32_t>( word( address ) ) | ( static_cast<std::uint32_t>( word( address + 2 ) ) << 16U );
}

} // namespace pgm::machine
