#include "pgm/cart/Bios.hpp"

#include <spdlog/fmt/fmt.h>

#include <cstddef>
#include <utility>

namespace pgm::cart
{

namespace
{

struct Expected
{
  char const* fileName;
  std::size_t size;
  /// The CRC-32 of the dump in MAME's `pgm` set.
  std::uint32_t crc;
};

// The program is version P0200 of the BIOS, the one the RTL simulation loads;
// MAME's set also holds P0100 as pgm_p01s.u20, which nothing here asks for.
constexpr Expected PROGRAM{ .fileName = "pgm_p02s.u20", .size = 0x20000, .crc = 0x78c15fa2 };
constexpr Expected TILES{ .fileName = "pgm_t01s.rom", .size = 0x200000, .crc = 0x1a7123a0 };
constexpr Expected MUSIC{ .fileName = "pgm_m01s.rom", .size = 0x200000, .crc = 0x45ae7159 };

std::expected<BiosRom, std::string> loadRom( io::RomSources const& sources, Expected const& expected )
{
  auto data = sources.find( expected.fileName );
  if ( !data )
  {
    return std::unexpected( fmt::format( "BIOS file {} is in none of the BIOS sources", expected.fileName ) );
  }
  if ( data->size() != expected.size )
  {
    return std::unexpected( fmt::format(
        "BIOS file {} holds {} bytes where {} are expected", expected.fileName, data->size(), expected.size ) );
  }
  std::uint32_t const crc = io::crc32( *data );
  return BiosRom{ .fileName = expected.fileName,
                  .origin = sources.origin( expected.fileName ).value_or( std::filesystem::path{} ),
                  .data = std::move( *data ),
                  .crc = crc,
                  .known = crc == expected.crc };
}

} // namespace

std::expected<Bios, std::string> Bios::load( io::RomSources const& sources )
{
  Bios bios;
  for ( auto [rom, expected] : { std::pair{ &bios.mProgram, &PROGRAM },
                                 std::pair{ &bios.mTiles, &TILES },
                                 std::pair{ &bios.mMusic, &MUSIC } } )
  {
    auto loaded = loadRom( sources, *expected );
    if ( !loaded )
    {
      return std::unexpected( loaded.error() );
    }
    *rom = std::move( *loaded );
  }
  return bios;
}

BiosRom const& Bios::program() const
{
  return mProgram;
}

BiosRom const& Bios::tiles() const
{
  return mTiles;
}

BiosRom const& Bios::music() const
{
  return mMusic;
}

} // namespace pgm::cart
