#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pgm::test
{

/// One ROM entry to be written into a synthetic `.pgm`.
struct PgmRom
{
  std::uint32_t type{};
  std::uint32_t mapping{};
  std::vector<std::uint8_t> data;
};

/// What a synthetic `.pgm` holds. Written independently of the reader, from
/// docs/spec/pgm-format.md, so that a test of the reader checks the reader
/// against the specification rather than against itself.
struct PgmFile
{
  std::string shortName = "testcart";
  std::string year = "1997";
  std::uint32_t hardware = 0;
  std::string manufacturer = "IGS";
  std::string asciiLongName = "Test Cartridge";
  std::vector<PgmRom> roms;
  /// The bytes of the region block (§4), placed after the strings.
  std::optional<std::vector<std::uint8_t>> regionBlock;
};

/// The bytes of `file` laid out as PGMBuilder lays out a version 0x0021 image.
std::vector<std::uint8_t> write( PgmFile const& file );

/// Stores `value` little-endian at `at`, for tests that corrupt a header.
void poke32( std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value );

/// A region block of the ASIC3 kind: the default region and (four-cc, value) pairs.
std::vector<std::uint8_t> asic3RegionBlock( std::uint32_t defaultRegion,
                                            std::vector<std::pair<std::uint32_t, std::uint32_t>> const& regions );

/// A four-character code as the format holds it: the first character most significant.
consteval std::uint32_t fourCc( std::string_view text )
{
  return ( static_cast<std::uint32_t>( static_cast<unsigned char>( text[0] ) ) << 24U ) |
         ( static_cast<std::uint32_t>( static_cast<unsigned char>( text[1] ) ) << 16U ) |
         ( static_cast<std::uint32_t>( static_cast<unsigned char>( text[2] ) ) << 8U ) |
         static_cast<std::uint32_t>( static_cast<unsigned char>( text[3] ) );
}

} // namespace pgm::test
