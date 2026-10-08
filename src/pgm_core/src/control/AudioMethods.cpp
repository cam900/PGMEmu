#include "Methods.hpp"

#include "machine/ClockEnables.hpp"

#include <spdlog/fmt/fmt.h>

#include <array>
#include <cmath>
#include <fstream>
#include <memory>
#include <string>

namespace pgm::control
{

namespace
{

constexpr std::size_t WAV_HEADER_SIZE = 44;

void putLittleEndian( std::ostream& out, std::uint32_t value, int bytes )
{
  for ( int i = 0; i < bytes; ++i )
  {
    out.put( static_cast<char>( ( value >> ( 8 * i ) ) & 0xffU ) );
  }
}

/// A 16-bit stereo WAV being written as the machine runs. Its header is
/// written again when it is closed, with the sizes and the rate.
class WavRecorder
{
public:
  explicit WavRecorder( std::string path ) : mPath{ std::move( path ) }, mFile{ mPath, std::ios::binary }
  {
    writeHeader( 0 );
  }

  [[nodiscard]] bool good() const
  {
    return mFile.good();
  }

  [[nodiscard]] std::string const& path() const
  {
    return mPath;
  }

  void append( std::span<machine::AudioFrame const> frames )
  {
    for ( machine::AudioFrame const& frame : frames )
    {
      putLittleEndian( mFile, static_cast<std::uint16_t>( frame.left ), 2 );
      putLittleEndian( mFile, static_cast<std::uint16_t>( frame.right ), 2 );
      if ( mFrames == 0 )
      {
        mFirstAt = frame.at;
      }
      mLastAt = frame.at;
      ++mFrames;
    }
  }

  /// Completes the header and closes the file. The rate it records is the
  /// one the frames came at, or `fallbackRate` when there were too few to tell.
  std::uint32_t close( double fallbackRate )
  {
    double const measured =
        mFrames > 1 && mLastAt > mFirstAt
            ? machine::CE_33M_HZ * static_cast<double>( mFrames - 1 ) / static_cast<double>( mLastAt - mFirstAt )
            : fallbackRate;
    auto const rate = static_cast<std::uint32_t>( std::lround( measured ) );
    mFile.seekp( 0 );
    writeHeader( rate );
    mFile.close();
    return rate;
  }

  [[nodiscard]] std::uint64_t frames() const
  {
    return mFrames;
  }

private:
  void writeHeader( std::uint32_t rate )
  {
    auto const dataBytes = static_cast<std::uint32_t>( mFrames * 4 );
    mFile.write( "RIFF", 4 );
    putLittleEndian( mFile, static_cast<std::uint32_t>( WAV_HEADER_SIZE - 8 ) + dataBytes, 4 );
    mFile.write( "WAVEfmt ", 8 );
    putLittleEndian( mFile, 16, 4 );       // the fmt chunk's size
    putLittleEndian( mFile, 1, 2 );        // PCM
    putLittleEndian( mFile, 2, 2 );        // channels
    putLittleEndian( mFile, rate, 4 );     // frames per second
    putLittleEndian( mFile, rate * 4, 4 ); // bytes per second
    putLittleEndian( mFile, 4, 2 );        // bytes per frame
    putLittleEndian( mFile, 16, 2 );       // bits per sample
    mFile.write( "data", 4 );
    putLittleEndian( mFile, dataBytes, 4 );
  }

  std::string mPath;
  std::ofstream mFile;
  std::uint64_t mFrames{};
  std::int64_t mFirstAt{};
  std::int64_t mLastAt{};
};

Error notLoaded()
{
  return Error{ .code = "not_loaded", .message = "No game is loaded" };
}

Json voiceJson( machine::Ics2115Voice const& v )
{
  return Json{ { "osc_acc", v.oscAcc },   { "osc_fc", v.oscFc },       { "osc_start", v.oscStart },
               { "osc_end", v.oscEnd },   { "osc_saddr", v.oscSaddr }, { "osc_conf", v.oscConf },
               { "osc_ctl", v.oscCtl },   { "vol_acc", v.volAcc },     { "vol_start", v.volStart },
               { "vol_end", v.volEnd },   { "vol_incr", v.volIncr },   { "vol_pan", v.volPan },
               { "vol_ctrl", v.volCtrl }, { "vol_mode", v.volMode } };
}

} // namespace

void addAudioMethods( Dispatcher& dispatcher, Emulator& emulator )
{
  // One capture at a time, shared by the two methods and the listener that
  // feeds it.
  auto recorder = std::make_shared<std::shared_ptr<WavRecorder>>();

  dispatcher.add( "audio.capture_start",
                  info( "Starts recording the sound to a WAV file at the ICS2115's own rate, about 33 kHz; runs add to "
                        "it as they end.",
                        { { .name = "path", .type = "string", .description = "Where to write the WAV." } } ),
                  [&emulator, recorder]( Json const& params ) -> Outcome
                  {
                    machine::Machine* const machine = emulator.machine();
                    auto const path = requireString( params, "path" );
                    if ( machine == nullptr || !path )
                    {
                      return std::unexpected( machine == nullptr ? notLoaded() : path.error() );
                    }
                    if ( *recorder )
                    {
                      return std::unexpected( Error{ .code = "capture_running",
                                                     .message = "A capture is running to " + ( *recorder )->path() } );
                    }
                    auto started = std::make_shared<WavRecorder>( *path );
                    if ( !started->good() )
                    {
                      return std::unexpected(
                          Error{ .code = "capture_failed", .message = fmt::format( "Cannot write {}", *path ) } );
                    }
                    *recorder = started;
                    machine->setAudioListener( [started]( std::span<machine::AudioFrame const> frames )
                                               { started->append( frames ); } );
                    return Json{ { "path", *path } };
                  } );

  dispatcher.add(
      "audio.capture_stop",
      info( "Ends the sound recording and completes the WAV file." ),
      [&emulator, recorder]( Json const& /*params*/ ) -> Outcome
      {
        if ( !*recorder )
        {
          return std::unexpected( Error{ .code = "capture_not_running", .message = "No capture is running" } );
        }
        machine::Machine* const machine = emulator.machine();
        // All 32 voices, the power-up setting, when the game has gone.
        double rate = machine::CE_33M_HZ / ( 32 * 32 );
        if ( machine != nullptr )
        {
          machine->setAudioListener( {} );
          rate = machine->audioRate();
        }
        WavRecorder& stopped = **recorder;
        std::uint32_t const recorded = stopped.close( rate );
        Json result{ { "path", stopped.path() }, { "frames", stopped.frames() }, { "sample_rate", recorded } };
        *recorder = nullptr;
        return result;
      } );

  dispatcher.add( "audio.voices",
                  info( "The ICS2115's active voices and their registers." ),
                  [&emulator]( Json const& /*params*/ ) -> Outcome
                  {
                    machine::Machine const* const machine = emulator.machine();
                    if ( machine == nullptr )
                    {
                      return std::unexpected( notLoaded() );
                    }
                    Json voices = Json::array();
                    for ( std::size_t v = 0; v < machine->ics2115ActiveVoices(); ++v )
                    {
                      voices.push_back( voiceJson( machine->ics2115Voices().at( v ) ) );
                    }
                    return Json{ { "active", machine->ics2115ActiveVoices() }, { "voices", std::move( voices ) } };
                  } );
}

} // namespace pgm::control
