#include "pgm/control/Batch.hpp"

#include <miniz.h>
#include <spdlog/fmt/fmt.h>

#include <filesystem>
#include <span>
#include <utility>

namespace pgm::control
{

namespace
{

std::string crcText( mz_ulong crc )
{
  return fmt::format( "{:08x}", static_cast<std::uint32_t>( crc ) );
}

std::string crcOf( std::span<std::uint8_t const> bytes )
{
  return crcText( mz_crc32( MZ_CRC32_INIT, bytes.data(), bytes.size() ) );
}

/// The sound a script's machine produces, summed as it comes.
struct SoundSum
{
  machine::Machine* listenedTo{};
  mz_ulong crc{ MZ_CRC32_INIT };

  void add( std::span<machine::AudioFrame const> frames )
  {
    for ( machine::AudioFrame const& frame : frames )
    {
      std::array<std::uint8_t, 4> bytes{};
      for ( std::size_t i = 0; i < 2; ++i )
      {
        auto const sample = static_cast<std::uint16_t>( i == 0 ? frame.left : frame.right );
        bytes.at( i * 2 ) = static_cast<std::uint8_t>( sample );
        bytes.at( ( i * 2 ) + 1 ) = static_cast<std::uint8_t>( sample >> 8U );
      }
      crc = mz_crc32( crc, bytes.data(), bytes.size() );
    }
  }
};

BatchResult failed( BatchResult result, BatchResult::Outcome outcome, std::string message )
{
  result.outcome = outcome;
  result.message = std::move( message );
  return result;
}

} // namespace

BatchResult runBatch( Settings settings, Json const& script )
{
  BatchResult result;
  if ( !script.is_object() || !script.contains( "steps" ) || !script.at( "steps" ).is_array() )
  {
    return failed( std::move( result ), BatchResult::Outcome::FAILED, "A script is an object with steps" );
  }
  if ( script.contains( "bios_program" ) )
  {
    std::filesystem::path const program{ script.at( "bios_program" ).get<std::string>() };
    if ( !std::filesystem::exists( program / "pgm_p02s.u20" ) )
    {
      return failed(
          std::move( result ),
          BatchResult::Outcome::MISSING,
          fmt::format( "{} holds no BIOS program; build it with scripts/make-pgmtest.sh", program.string() ) );
    }
    settings.biosSources.insert( settings.biosSources.begin(), program );
  }

  Emulator emulator{ std::move( settings ) };
  Dispatcher const dispatcher{ emulator };
  auto sound = std::make_shared<SoundSum>();
  std::size_t index = 0;
  for ( Json const& step : script.at( "steps" ) )
  {
    ++index;
    if ( step.contains( "checkpoint" ) )
    {
      machine::Machine const* const machine = emulator.machine();
      if ( machine == nullptr )
      {
        return failed( std::move( result ),
                       BatchResult::Outcome::FAILED,
                       fmt::format( "step {}: a checkpoint before a game is loaded", index ) );
      }
      result.checkpoints.push_back( Checkpoint{ .name = step.at( "checkpoint" ).get<std::string>(),
                                                .sums = Json{ { "frame", machine->frame() },
                                                              { "picture", crcOf( machine->picture() ) },
                                                              { "work_ram", crcOf( machine->workRam() ) },
                                                              { "video_ram", crcOf( machine->videoRam() ) },
                                                              { "palette_ram", crcOf( machine->paletteRam() ) },
                                                              { "audio_ram", crcOf( machine->z80Ram() ) },
                                                              { "sound", crcText( sound->crc ) } } } );
      sound->crc = MZ_CRC32_INIT;
      continue;
    }

    if ( !step.contains( "method" ) )
    {
      return failed( std::move( result ),
                     BatchResult::Outcome::FAILED,
                     fmt::format( "step {}: neither a request nor a checkpoint", index ) );
    }
    Json request{ { "id", index }, { "method", step.at( "method" ) } };
    if ( step.contains( "params" ) )
    {
      request["params"] = step.at( "params" );
    }
    Json const response = dispatcher.handle( request );
    if ( response.at( "ok" ) != true )
    {
      auto const& error = response.at( "error" );
      auto const code = error.at( "code" ).get<std::string>();
      bool const missing = code == "unknown_game" || code == "load_failed";
      return failed( std::move( result ),
                     missing ? BatchResult::Outcome::MISSING : BatchResult::Outcome::FAILED,
                     fmt::format( "step {} ({}): {}: {}",
                                  index,
                                  step.at( "method" ).get<std::string>(),
                                  code,
                                  error.at( "message" ).get<std::string>() ) );
    }

    // A game loaded is a machine of its own; its sound is summed from then on.
    machine::Machine* const machine = emulator.machine();
    if ( machine != nullptr && machine != sound->listenedTo )
    {
      machine->setAudioListener( [sound]( std::span<machine::AudioFrame const> frames ) { sound->add( frames ); } );
      sound->listenedTo = machine;
      sound->crc = MZ_CRC32_INIT;
    }
  }
  result.outcome = BatchResult::Outcome::RAN;
  return result;
}

std::vector<std::string> compareWithGolden( BatchResult const& result, Json const& golden )
{
  std::vector<std::string> differences;
  for ( Checkpoint const& checkpoint : result.checkpoints )
  {
    if ( !golden.is_object() || !golden.contains( checkpoint.name ) )
    {
      differences.push_back( fmt::format( "{}: has no golden sums", checkpoint.name ) );
      continue;
    }
    Json const& expected = golden.at( checkpoint.name );
    for ( auto const& [key, value] : checkpoint.sums.items() )
    {
      if ( !expected.contains( key ) || expected.at( key ) != value )
      {
        differences.push_back( fmt::format( "{}: {} is {}, golden {}",
                                            checkpoint.name,
                                            key,
                                            value.dump(),
                                            expected.contains( key ) ? expected.at( key ).dump() : "absent" ) );
      }
    }
  }
  if ( golden.is_object() )
  {
    for ( auto const& [name, sums] : golden.items() )
    {
      bool const reached = std::ranges::any_of(
          result.checkpoints, [&name]( Checkpoint const& checkpoint ) { return checkpoint.name == name; } );
      if ( !reached )
      {
        differences.push_back( fmt::format( "{}: not reached", name ) );
      }
    }
  }
  return differences;
}

Json goldenOf( BatchResult const& result )
{
  Json golden = Json::object();
  for ( Checkpoint const& checkpoint : result.checkpoints )
  {
    golden[checkpoint.name] = checkpoint.sums;
  }
  return golden;
}

} // namespace pgm::control
