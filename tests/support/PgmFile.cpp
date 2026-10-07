#include "support/PgmFile.hpp"

#include <algorithm>
#include <cstddef>

namespace pgm::test
{

namespace
{

constexpr std::size_t HEADER_SIZE = 1024;
constexpr std::size_t INFO_SIZE = 76;
constexpr std::size_t ALIGNMENT = 512;

void putString( std::vector<std::uint8_t>& bytes, std::size_t at, std::string const& text, std::size_t size )
{
  std::copy_n( text.begin(), std::min( text.size(), size ), bytes.begin() + static_cast<std::ptrdiff_t>( at ) );
}

/// Appends `text` and its NUL to the header's string area at `cursor`, and
/// answers where it went.
std::uint32_t appendString( std::vector<std::uint8_t>& bytes, std::size_t& cursor, std::string const& text )
{
  if ( text.empty() )
  {
    return 0;
  }
  auto const at = static_cast<std::uint32_t>( cursor );
  putString( bytes, cursor, text, text.size() );
  cursor += text.size() + 1;
  return at;
}

} // namespace

void poke32( std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value )
{
  for ( std::size_t i = 0; i < 4; ++i )
  {
    bytes[at + i] = static_cast<std::uint8_t>( value >> ( 8U * i ) );
  }
}

std::vector<std::uint8_t> write( PgmFile const& file )
{
  std::vector<std::uint8_t> bytes( HEADER_SIZE, 0 );
  putString( bytes, 0, "IGSPGM", 6 );
  bytes[6] = 0x00; // version, big-endian
  bytes[7] = 0x21;
  poke32( bytes, 8, INFO_SIZE );
  putString( bytes, 12, file.shortName, 16 );
  putString( bytes, 40, file.year, 4 );
  poke32( bytes, 44, file.hardware );

  // ROM data follows the header, each section at a multiple of 512.
  std::size_t cursor = INFO_SIZE;
  poke32( bytes, 52, file.roms.empty() ? 0 : static_cast<std::uint32_t>( cursor ) );
  poke32( bytes, 56, static_cast<std::uint32_t>( file.roms.size() ) );
  for ( PgmRom const& rom : file.roms )
  {
    std::size_t const offset = ( bytes.size() + ALIGNMENT - 1 ) / ALIGNMENT * ALIGNMENT;
    bytes.resize( offset, 0 );
    bytes.insert( bytes.end(), rom.data.begin(), rom.data.end() );
    poke32( bytes, cursor, rom.type );
    poke32( bytes, cursor + 4, rom.mapping );
    poke32( bytes, cursor + 8, static_cast<std::uint32_t>( offset ) );
    poke32( bytes, cursor + 12, static_cast<std::uint32_t>( rom.data.size() ) );
    cursor += 16;
  }

  if ( file.regionBlock )
  {
    poke32( bytes, 72, static_cast<std::uint32_t>( cursor ) );
    std::ranges::copy( *file.regionBlock, bytes.begin() + static_cast<std::ptrdiff_t>( cursor ) );
    cursor += file.regionBlock->size();
  }

  poke32( bytes, 28, appendString( bytes, cursor, file.manufacturer ) );
  poke32( bytes, 32, appendString( bytes, cursor, file.asciiLongName ) );
  return bytes;
}

std::vector<std::uint8_t> asic3RegionBlock( std::uint32_t defaultRegion,
                                            std::vector<std::pair<std::uint32_t, std::uint32_t>> const& regions )
{
  std::vector<std::uint8_t> block( 8 + ( regions.size() * 8 ), 0 );
  block[0] = 2; // ASIC3
  block[1] = static_cast<std::uint8_t>( regions.size() );
  block[2] = 8; // the table follows the 8-byte info
  poke32( block, 4, defaultRegion );
  for ( std::size_t i = 0; i < regions.size(); ++i )
  {
    poke32( block, 8 + ( i * 8 ), regions[i].first );
    poke32( block, 12 + ( i * 8 ), regions[i].second );
  }
  return block;
}

} // namespace pgm::test
