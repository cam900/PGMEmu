#include "Methods.hpp"

#include <miniz.h>
#include <spdlog/fmt/fmt.h>

#include <array>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace pgm::control
{

namespace
{

constexpr std::size_t WIDTH = 448;
constexpr std::size_t HEIGHT = 224;

/// The picture as a PNG of RGB pixels, as the RTL simulator writes it.
std::vector<std::uint8_t> encodePng( std::span<std::uint8_t const> rgba )
{
  std::vector<std::uint8_t> rgb;
  rgb.reserve( WIDTH * HEIGHT * 3 );
  for ( std::size_t at = 0; at + 3 < rgba.size(); at += 4 )
  {
    rgb.insert( rgb.end(), { rgba[at], rgba[at + 1], rgba[at + 2] } );
  }
  std::size_t size = 0;
  void* const png =
      tdefl_write_image_to_png_file_in_memory_ex( rgb.data(), WIDTH, HEIGHT, 3, &size, MZ_DEFAULT_LEVEL, 0 );
  if ( png == nullptr )
  {
    return {};
  }
  std::vector<std::uint8_t> bytes( static_cast<std::uint8_t*>( png ), static_cast<std::uint8_t*>( png ) + size );
  mz_free( png );
  return bytes;
}

std::string base64( std::span<std::uint8_t const> bytes )
{
  static constexpr std::string_view ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string text;
  text.reserve( ( ( bytes.size() + 2 ) / 3 ) * 4 );
  for ( std::size_t at = 0; at < bytes.size(); at += 3 )
  {
    std::uint32_t const chunk = ( static_cast<std::uint32_t>( bytes[at] ) << 16U ) |
                                ( at + 1 < bytes.size() ? static_cast<std::uint32_t>( bytes[at + 1] ) << 8U : 0U ) |
                                ( at + 2 < bytes.size() ? bytes[at + 2] : 0U );
    text.push_back( ALPHABET[( chunk >> 18U ) & 63U] );
    text.push_back( ALPHABET[( chunk >> 12U ) & 63U] );
    text.push_back( at + 1 < bytes.size() ? ALPHABET[( chunk >> 6U ) & 63U] : '=' );
    text.push_back( at + 2 < bytes.size() ? ALPHABET[chunk & 63U] : '=' );
  }
  return text;
}

} // namespace

void addVideoMethods( Dispatcher& dispatcher, Emulator& emulator )
{
  dispatcher.add(
      "video.screenshot",
      [&emulator]( Json const& params ) -> Outcome
      {
        machine::Machine const* const machine = emulator.machine();
        if ( machine == nullptr )
        {
          return std::unexpected( Error{ .code = "not_loaded", .message = "No game is loaded" } );
        }
        std::vector<std::uint8_t> const png = encodePng( machine->picture() );
        if ( png.empty() )
        {
          return std::unexpected( Error{ .code = "screenshot_failed", .message = "The picture could not be encoded" } );
        }

        Json result{ { "width", WIDTH }, { "height", HEIGHT }, { "frame", machine->picturesDrawn() } };
        if ( params.contains( "path" ) )
        {
          auto const path = requireString( params, "path" );
          if ( !path )
          {
            return std::unexpected( path.error() );
          }
          std::ofstream file{ *path, std::ios::binary };
          file.write( reinterpret_cast<char const*>( png.data() ), static_cast<std::streamsize>( png.size() ) );
          if ( !file )
          {
            return std::unexpected(
                Error{ .code = "screenshot_failed", .message = fmt::format( "Cannot write {}", *path ) } );
          }
          result["path"] = *path;
        }
        else
        {
          result["png_base64"] = base64( png );
        }
        return result;
      } );
}

} // namespace pgm::control
