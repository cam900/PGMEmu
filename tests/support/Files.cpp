#include "support/Files.hpp"

#include <miniz.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace pgm::test
{

TemporaryDirectory::TemporaryDirectory()
{
  // The clock and a counter, so that two tests running at once, or one test
  // run twice in a row, never share a directory.
  static std::atomic<unsigned> counter{ 0 };
  auto const stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  mPath = std::filesystem::temp_directory_path() /
          ( "pgmemu-test-" + std::to_string( stamp ) + "-" + std::to_string( counter++ ) );
  std::filesystem::create_directories( mPath );
}

TemporaryDirectory::~TemporaryDirectory()
{
  std::error_code ignored;
  std::filesystem::remove_all( mPath, ignored );
}

std::filesystem::path const& TemporaryDirectory::path() const
{
  return mPath;
}

void writeFile( std::filesystem::path const& path, std::vector<std::uint8_t> const& bytes )
{
  std::ofstream stream{ path, std::ios::binary };
  stream.write( reinterpret_cast<char const*>( bytes.data() ), static_cast<std::streamsize>( bytes.size() ) );
  if ( !stream )
  {
    throw std::runtime_error( "cannot write " + path.string() );
  }
}

void writeZip( std::filesystem::path const& path,
               std::vector<std::pair<std::string, std::vector<std::uint8_t>>> const& files )
{
  mz_zip_archive zip{};
  if ( mz_zip_writer_init_file( &zip, path.string().c_str(), 0 ) == 0 )
  {
    throw std::runtime_error( "cannot create " + path.string() );
  }
  for ( auto const& [name, bytes] : files )
  {
    mz_zip_writer_add_mem( &zip, name.c_str(), bytes.data(), bytes.size(), static_cast<mz_uint>( MZ_DEFAULT_LEVEL ) );
  }
  bool const finished = mz_zip_writer_finalize_archive( &zip ) != 0;
  mz_zip_writer_end( &zip );
  if ( !finished )
  {
    throw std::runtime_error( "cannot finish " + path.string() );
  }
}

} // namespace pgm::test
