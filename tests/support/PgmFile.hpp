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

/// A region block of the ASIC27 kind: where the region is patched, and (four-cc, value) pairs.
std::vector<std::uint8_t> asic27RegionBlock( std::uint16_t patchType,
                                             std::uint16_t patchOffset,
                                             std::vector<std::pair<std::uint32_t, std::uint32_t>> const& regions );

/// A region block of the IGS025 kind: (four-cc, value) pairs.
std::vector<std::uint8_t> igs025RegionBlock( std::vector<std::pair<std::uint32_t, std::uint32_t>> const& regions );

/// One table of an I25 block: its region value, its game id, and every byte of
/// its 0xEC filled with `fill` plus its index.
struct Igs025TableSpec
{
  std::uint8_t region{};
  std::uint32_t gameId{};
  std::uint8_t fill{};
};

/// An I25 block (§3.1): the variant, the default region and the tables.
std::vector<std::uint8_t>
igs025Block( std::uint8_t variant, std::uint8_t defaultRegion, std::vector<Igs025TableSpec> const& tables );

/// A four-character code as the format holds it: the first character most significant.
consteval std::uint32_t fourCc( std::string_view text )
{
  return ( static_cast<std::uint32_t>( static_cast<unsigned char>( text[0] ) ) << 24U ) |
         ( static_cast<std::uint32_t>( static_cast<unsigned char>( text[1] ) ) << 16U ) |
         ( static_cast<std::uint32_t>( static_cast<unsigned char>( text[2] ) ) << 8U ) |
         static_cast<std::uint32_t>( static_cast<unsigned char>( text[3] ) );
}

} // namespace pgm::test
