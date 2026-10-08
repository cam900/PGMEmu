#include "Methods.hpp"

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>

namespace pgm::control
{

namespace
{

/// The most one `memory.read` returns. Larger reads are made in pieces, so that
/// no single response line grows past a few megabytes of hex.
constexpr std::uint64_t MAX_READ = 0x100000;

std::string toHex( std::span<std::uint8_t const> bytes )
{
  static constexpr std::array<char, 16> DIGITS{ '0', '1', '2', '3', '4', '5', '6', '7',
                                                '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
  std::string hex;
  hex.reserve( bytes.size() * 2 );
  for ( std::uint8_t const byte : bytes )
  {
    hex.push_back( DIGITS[byte >> 4U] );
    hex.push_back( DIGITS[byte & 0x0fU] );
  }
  return hex;
}

} // namespace

void addMemoryMethods( Dispatcher& dispatcher, Emulator& emulator )
{
  dispatcher.add( "memory.list_regions",
                  info( "The names of the memory regions memory.read can read now." ),
                  [&emulator]( Json const& /*params*/ ) -> Outcome
                  {
                    Json names = Json::array();
                    for ( MemoryRegion const& region : emulator.memoryRegions() )
                    {
                      names.push_back( region.name );
                    }
                    return names;
                  } );

  dispatcher.add(
      "memory.read",
      info( "Reads bytes of a memory region, at most 1 MB, answered as lowercase hex. Work RAM is in 68000 byte order: "
            "the high byte of a word first.",
            { { .name = "region",
                .type = "string",
                .description =
                    "A name from memory.list_regions, such as WORK_RAM, VIDEO_RAM, PALETTE_RAM or AUDIO_RAM." },
              { .name = "address", .type = "integer", .description = "The first byte's offset in the region." },
              { .name = "size", .type = "integer", .description = "How many bytes, at most 1048576." } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const name = requireString( params, "region" );
        if ( !name )
        {
          return std::unexpected( name.error() );
        }
        auto const address = requireUnsigned( params, "address" );
        if ( !address )
        {
          return std::unexpected( address.error() );
        }
        auto const size = requireUnsigned( params, "size" );
        if ( !size )
        {
          return std::unexpected( size.error() );
        }
        if ( *size > MAX_READ )
        {
          return std::unexpected(
              badRequest( fmt::format( "size {} is above the limit of {} bytes", *size, MAX_READ ) ) );
        }

        auto const regions = emulator.memoryRegions();
        auto const region = std::ranges::find( regions, *name, &MemoryRegion::name );
        if ( region == regions.end() )
        {
          return std::unexpected(
              Error{ .code = "invalid_region", .message = fmt::format( "No memory region {} is loaded", *name ) } );
        }
        if ( *address > region->bytes.size() || *size > region->bytes.size() - *address )
        {
          return std::unexpected( Error{
              .code = "invalid_range",
              .message = fmt::format(
                  "{} bytes at {} run past the {} bytes of {}", *size, *address, region->bytes.size(), *name ) } );
        }

        auto const bytes =
            region->bytes.subspan( static_cast<std::size_t>( *address ), static_cast<std::size_t>( *size ) );
        return Json{ { "region", *name }, { "address", *address }, { "data_hex", toHex( bytes ) } };
      } );
}

} // namespace pgm::control
