#include "Methods.hpp"

#include <miniz.h>
#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace pgm::control
{

namespace
{

// A save state file: this magic, a format version, the set name of the game it
// was taken from, and the machine's state (Machine::saveState) deflated.
constexpr std::string_view FILE_MAGIC = "PGMEMU-STATE";
constexpr std::uint32_t FILE_VERSION = 1;
constexpr std::string_view STATE_EXTENSION = ".pgmstate";

/// The work RAM's 128 KB, as the RTL's NVRAM interface reads and writes them.
constexpr std::size_t NVRAM_SIZE = 0x20000;

Error notLoaded()
{
  return Error{ .code = "not_loaded", .message = "No game is loaded" };
}

/// `filename` in the state directory, unless it names a directory of its own.
std::filesystem::path statePath( Emulator const& emulator, std::string const& filename )
{
  std::filesystem::path path{ filename };
  if ( path.has_parent_path() || emulator.settings().stateDirectory.empty() )
  {
    return path;
  }
  return emulator.settings().stateDirectory / path;
}

void putWord( std::vector<std::uint8_t>& out, std::uint32_t value )
{
  for ( int i = 0; i < 4; ++i )
  {
    out.push_back( static_cast<std::uint8_t>( value >> ( 8 * i ) ) );
  }
}

std::uint32_t getWord( std::vector<std::uint8_t> const& in, std::size_t at )
{
  std::uint32_t value = 0;
  for ( std::size_t i = 0; i < 4; ++i )
  {
    value |= static_cast<std::uint32_t>( in.at( at + i ) ) << ( 8 * i );
  }
  return value;
}

std::vector<std::uint8_t> readFile( std::filesystem::path const& path )
{
  std::ifstream file{ path, std::ios::binary };
  return { std::istreambuf_iterator<char>{ file }, std::istreambuf_iterator<char>{} };
}

bool writeFile( std::filesystem::path const& path, std::vector<std::uint8_t> const& bytes )
{
  std::ofstream file{ path, std::ios::binary };
  file.write( reinterpret_cast<char const*>( bytes.data() ), static_cast<std::streamsize>( bytes.size() ) );
  return file.good();
}

std::vector<std::uint8_t> packState( std::string const& game, std::vector<std::uint8_t> const& state )
{
  std::vector<std::uint8_t> file( FILE_MAGIC.begin(), FILE_MAGIC.end() );
  putWord( file, FILE_VERSION );
  putWord( file, static_cast<std::uint32_t>( game.size() ) );
  file.insert( file.end(), game.begin(), game.end() );
  putWord( file, static_cast<std::uint32_t>( state.size() ) );

  mz_ulong packedSize = mz_compressBound( static_cast<mz_ulong>( state.size() ) );
  std::vector<std::uint8_t> packed( packedSize );
  mz_compress2( packed.data(), &packedSize, state.data(), static_cast<mz_ulong>( state.size() ), MZ_BEST_SPEED );
  packed.resize( packedSize );
  file.insert( file.end(), packed.begin(), packed.end() );
  return file;
}

/// The game a state file was taken from and the machine state it holds, or
/// why it cannot be read.
std::expected<std::pair<std::string, std::vector<std::uint8_t>>, std::string>
unpackState( std::vector<std::uint8_t> const& file )
{
  std::size_t at = FILE_MAGIC.size();
  if ( file.size() < at + 8 || !std::equal( FILE_MAGIC.begin(), FILE_MAGIC.end(), file.begin() ) )
  {
    return std::unexpected( "it is not a PGMEmu save state" );
  }
  if ( getWord( file, at ) != FILE_VERSION )
  {
    return std::unexpected( "it is a save state of another format version" );
  }
  std::uint32_t const nameSize = getWord( file, at + 4 );
  at += 8;
  if ( file.size() < at + nameSize + 4 )
  {
    return std::unexpected( "it is cut short" );
  }
  std::string game( file.begin() + static_cast<std::ptrdiff_t>( at ),
                    file.begin() + static_cast<std::ptrdiff_t>( at + nameSize ) );
  at += nameSize;
  std::uint32_t const stateSize = getWord( file, at );
  at += 4;

  std::vector<std::uint8_t> state( stateSize );
  auto unpackedSize = static_cast<mz_ulong>( stateSize );
  if ( mz_uncompress( state.data(), &unpackedSize, file.data() + at, static_cast<mz_ulong>( file.size() - at ) ) !=
           MZ_OK ||
       unpackedSize != stateSize )
  {
    return std::unexpected( "its contents are damaged" );
  }
  return std::pair{ std::move( game ), std::move( state ) };
}

} // namespace

void addStateMethods( Dispatcher& dispatcher, Emulator& emulator )
{
  Param const filename{ .name = "filename",
                        .type = "string",
                        .description = "A file name in the state directory, or a path." };

  dispatcher.add(
      "state.save",
      info( "Saves the whole machine's state to a file, to be loaded later into the same game.", { filename } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        machine::Machine* const machine = emulator.machine();
        auto const name = requireString( params, "filename" );
        if ( machine == nullptr || !name )
        {
          return std::unexpected( machine == nullptr ? notLoaded() : name.error() );
        }
        auto const path = statePath( emulator, *name );
        if ( !writeFile( path, packState( emulator.gameName().value_or( "" ), machine->saveState() ) ) )
        {
          return std::unexpected(
              Error{ .code = "state_failed", .message = fmt::format( "Cannot write {}", path.string() ) } );
        }
        return Json::object();
      } );

  dispatcher.add(
      "state.load",
      info( "Restores a state that state.save wrote from the game now loaded.", { filename } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        machine::Machine* const machine = emulator.machine();
        auto const name = requireString( params, "filename" );
        if ( machine == nullptr || !name )
        {
          return std::unexpected( machine == nullptr ? notLoaded() : name.error() );
        }
        auto const path = statePath( emulator, *name );
        auto const file = readFile( path );
        if ( file.empty() )
        {
          return std::unexpected(
              Error{ .code = "state_failed", .message = fmt::format( "Cannot read {}", path.string() ) } );
        }
        auto const unpacked = unpackState( file );
        if ( !unpacked )
        {
          return std::unexpected(
              Error{ .code = "state_mismatch", .message = fmt::format( "{}: {}", path.string(), unpacked.error() ) } );
        }
        if ( unpacked->first != emulator.gameName().value_or( "" ) )
        {
          return std::unexpected( Error{ .code = "state_mismatch",
                                         .message = fmt::format( "{} was saved from {}, and {} is loaded",
                                                                 path.string(),
                                                                 unpacked->first,
                                                                 emulator.gameName().value_or( "" ) ) } );
        }
        if ( !machine->loadState( unpacked->second ) )
        {
          return std::unexpected(
              Error{ .code = "state_mismatch",
                     .message = fmt::format( "{} was saved by another build of the emulator", path.string() ) } );
        }
        return Json::object();
      } );

  dispatcher.add( "state.list",
                  info( "The save states in the state directory." ),
                  [&emulator]( Json const& /*params*/ ) -> Outcome
                  {
                    std::filesystem::path const directory = emulator.settings().stateDirectory.empty()
                                                                ? std::filesystem::path{ "." }
                                                                : emulator.settings().stateDirectory;
                    std::vector<std::string> names;
                    std::error_code failed;
                    for ( auto const& entry : std::filesystem::directory_iterator( directory, failed ) )
                    {
                      if ( entry.path().extension() == STATE_EXTENSION )
                      {
                        names.push_back( entry.path().filename().string() );
                      }
                    }
                    std::ranges::sort( names );
                    return Json{ { "states", names } };
                  } );

  dispatcher.add(
      "nvram.save",
      info( "Saves the battery-backed work RAM, 128 KB, as the RTL's NVRAM interface lays it out: each word's low "
            "byte first.",
            { filename } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        machine::Machine const* const machine = emulator.machine();
        auto const name = requireString( params, "filename" );
        if ( machine == nullptr || !name )
        {
          return std::unexpected( machine == nullptr ? notLoaded() : name.error() );
        }
        auto const ram = machine->workRam();
        std::vector<std::uint8_t> bytes( NVRAM_SIZE );
        for ( std::size_t at = 0; at < NVRAM_SIZE; ++at )
        {
          bytes[at] = ram[at ^ 1U];
        }
        auto const path = statePath( emulator, *name );
        if ( !writeFile( path, bytes ) )
        {
          return std::unexpected(
              Error{ .code = "nvram_failed", .message = fmt::format( "Cannot write {}", path.string() ) } );
        }
        return Json::object();
      } );

  dispatcher.add( "nvram.load",
                  info( "Loads the work RAM from a file nvram.save wrote.", { filename } ),
                  [&emulator]( Json const& params ) -> Outcome
                  {
                    machine::Machine* const machine = emulator.machine();
                    auto const name = requireString( params, "filename" );
                    if ( machine == nullptr || !name )
                    {
                      return std::unexpected( machine == nullptr ? notLoaded() : name.error() );
                    }
                    auto const path = statePath( emulator, *name );
                    auto const bytes = readFile( path );
                    if ( bytes.size() != NVRAM_SIZE )
                    {
                      return std::unexpected(
                          Error{ .code = "nvram_failed",
                                 .message = fmt::format( "{} is not a 128 KB NVRAM file", path.string() ) } );
                    }
                    std::vector<std::uint8_t> ram( NVRAM_SIZE );
                    for ( std::size_t at = 0; at < NVRAM_SIZE; ++at )
                    {
                      ram[at] = bytes[at ^ 1U];
                    }
                    machine->setWorkRam( ram );
                    return Json::object();
                  } );
}

} // namespace pgm::control
