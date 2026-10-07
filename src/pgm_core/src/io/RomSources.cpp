#include "pgm/io/RomSources.hpp"

#include <miniz.h>
#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace pgm::io
{

namespace
{

/// A zip archive open for reading, closed when it goes out of scope. Archives
/// are opened per lookup rather than kept open: a game is loaded a few files at
/// a time, and an open handle per place would be state the emulator carries for
/// nothing.
class ZipReader
{
public:
  explicit ZipReader( std::filesystem::path const& path )
  {
    mOpen = mz_zip_reader_init_file( &mArchive, path.string().c_str(), 0 ) != 0;
  }

  ~ZipReader()
  {
    if ( mOpen )
    {
      mz_zip_reader_end( &mArchive );
    }
  }

  ZipReader( ZipReader const& ) = delete;
  ZipReader& operator=( ZipReader const& ) = delete;
  ZipReader( ZipReader&& ) = delete;
  ZipReader& operator=( ZipReader&& ) = delete;

  [[nodiscard]] bool isOpen() const
  {
    return mOpen;
  }

  [[nodiscard]] std::vector<std::string> names()
  {
    std::vector<std::string> names;
    mz_uint const count = mz_zip_reader_get_num_files( &mArchive );
    for ( mz_uint i = 0; i < count; ++i )
    {
      mz_zip_archive_file_stat stat{};
      if ( mz_zip_reader_file_stat( &mArchive, i, &stat ) != 0 && stat.m_is_directory == 0 )
      {
        names.emplace_back( stat.m_filename );
      }
    }
    return names;
  }

  [[nodiscard]] std::optional<std::vector<std::uint8_t>> extract( std::string const& name )
  {
    int const index = mz_zip_reader_locate_file( &mArchive, name.c_str(), nullptr, 0 );
    mz_zip_archive_file_stat stat{};
    if ( index < 0 || mz_zip_reader_file_stat( &mArchive, static_cast<mz_uint>( index ), &stat ) == 0 )
    {
      return std::nullopt;
    }
    std::vector<std::uint8_t> bytes( static_cast<std::size_t>( stat.m_uncomp_size ) );
    if ( mz_zip_reader_extract_to_mem( &mArchive, static_cast<mz_uint>( index ), bytes.data(), bytes.size(), 0 ) == 0 )
    {
      return std::nullopt;
    }
    return bytes;
  }

private:
  mz_zip_archive mArchive{};
  bool mOpen{};
};

std::optional<std::vector<std::uint8_t>> readFile( std::filesystem::path const& path )
{
  std::ifstream stream{ path, std::ios::binary };
  if ( !stream )
  {
    return std::nullopt;
  }
  std::vector<std::uint8_t> bytes{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
  if ( stream.bad() )
  {
    return std::nullopt;
  }
  return bytes;
}

} // namespace

std::uint32_t crc32( std::span<std::uint8_t const> bytes )
{
  return static_cast<std::uint32_t>( mz_crc32( MZ_CRC32_INIT, bytes.data(), bytes.size() ) );
}

std::expected<RomSources, std::string> RomSources::open( std::span<std::filesystem::path const> places )
{
  RomSources sources;
  for ( std::filesystem::path const& path : places )
  {
    std::error_code failed;
    if ( std::filesystem::is_directory( path, failed ) )
    {
      sources.mPlaces.push_back( Place{ .path = path, .zip = false, .entries = {} } );
      continue;
    }
    if ( !std::filesystem::is_regular_file( path, failed ) )
    {
      return std::unexpected( fmt::format( "{} is neither a directory nor a file", path.string() ) );
    }
    ZipReader zip{ path };
    if ( !zip.isOpen() )
    {
      return std::unexpected( fmt::format( "{} cannot be opened as a zip archive", path.string() ) );
    }
    sources.mPlaces.push_back( Place{ .path = path, .zip = true, .entries = zip.names() } );
  }
  return sources;
}

std::optional<std::vector<std::uint8_t>> RomSources::find( std::string_view name ) const
{
  Place const* const place = placeOf( name );
  if ( place == nullptr )
  {
    return std::nullopt;
  }
  if ( !place->zip )
  {
    return readFile( place->path / name );
  }
  ZipReader zip{ place->path };
  return zip.isOpen() ? zip.extract( std::string{ name } ) : std::nullopt;
}

std::optional<std::filesystem::path> RomSources::origin( std::string_view name ) const
{
  Place const* const place = placeOf( name );
  if ( place == nullptr )
  {
    return std::nullopt;
  }
  return place->path;
}

RomSources::Place const* RomSources::placeOf( std::string_view name ) const
{
  for ( Place const& place : mPlaces )
  {
    if ( place.zip ? std::ranges::find( place.entries, name ) != place.entries.end()
                   : std::filesystem::is_regular_file( place.path / name ) )
    {
      return &place;
    }
  }
  return nullptr;
}

} // namespace pgm::io
