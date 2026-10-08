#include "EmulationThread.hpp"

#include "pgm/machine/Time.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace pgm::app
{

namespace
{

/// The board's frame rate, some 59.19 Hz.
constexpr double FRAMES_PER_SECOND =
    static_cast<double>( machine::UNITS_PER_SECOND ) / static_cast<double>( machine::UNITS_PER_FRAME );

/// The most frames run to catch up: after a stall, such as a long request, the
/// rest of the backlog is dropped rather than run at full speed.
constexpr double MAX_FRAMES_OWED = 4.0;

/// How long the thread sleeps at most while it has no frames to run, so that
/// it notices a game loaded or a pause lifted.
constexpr std::chrono::milliseconds IDLE_WAIT{ 20 };

/// How far back a rewind goes: 30 seconds, some 1800 states of 1.4 to 1.8 MB.
constexpr auto HISTORY_FRAMES = static_cast<std::size_t>( 30.0 * FRAMES_PER_SECOND );

std::uint64_t packInputs( std::array<std::uint16_t, 4> const& pressed )
{
  std::uint64_t packed = 0;
  for ( std::size_t i = 0; i < pressed.size(); ++i )
  {
    packed |= static_cast<std::uint64_t>( pressed.at( i ) ) << ( 16 * i );
  }
  return packed;
}

std::array<std::uint16_t, 4> unpackInputs( std::uint64_t packed )
{
  std::array<std::uint16_t, 4> pressed{};
  for ( std::size_t i = 0; i < pressed.size(); ++i )
  {
    pressed.at( i ) = static_cast<std::uint16_t>( packed >> ( 16 * i ) );
  }
  return pressed;
}

} // namespace

EmulationThread::EmulationThread( Settings settings, AudioOutput* audio )
    : mEmulator{ std::move( settings ) }, mDispatcher{ mEmulator }, mMethods{ mDispatcher.methods() }, mAudio{ audio },
      mThread{ [this] { loop(); } }
{
}

EmulationThread::~EmulationThread()
{
  {
    std::scoped_lock const lock{ mMutex };
    mStop = true;
  }
  mWake.notify_all();
  mThread.join();
}

std::vector<control::Method> const& EmulationThread::methods() const
{
  return mMethods;
}

control::Json EmulationThread::handle( control::Json const& request )
{
  std::future<control::Json> answer;
  {
    std::scoped_lock const lock{ mMutex };
    if ( mStop )
    {
      return control::Json{ { "id", 0 },
                            { "ok", false },
                            { "error", { { "code", "shutting_down" }, { "message", "The emulator is closing" } } } };
    }
    mPending.push_back( Pending{ .request = request, .answer = {} } );
    answer = mPending.back().answer.get_future();
  }
  mWake.notify_all();
  return answer.get();
}

void EmulationThread::setPaused( bool paused )
{
  mPaused = paused;
  if ( paused && mAudio != nullptr )
  {
    // So that sound resumes at once, not after what was queued.
    mAudio->clear();
  }
  mWake.notify_all();
}

void EmulationThread::setHostInputs( std::array<std::uint16_t, 4> const& pressed )
{
  mHostInputs = packInputs( pressed );
}

void EmulationThread::setRewindKept( bool kept )
{
  mRewindKept = kept;
}

void EmulationThread::setRunAhead( int frames )
{
  mRunAhead = frames;
}

void EmulationThread::setRewinding( bool held )
{
  if ( held && !mRewinding.exchange( true ) && mAudio != nullptr )
  {
    // Silent while rewinding, from the first step back.
    mAudio->clear();
  }
  mRewinding = held;
  mWake.notify_all();
}

bool EmulationThread::takePicture( std::int64_t& picturesSeen, std::vector<std::uint8_t>& out )
{
  std::scoped_lock const lock{ mPictureMutex };
  if ( mPicturesDrawn == picturesSeen || mPicture.empty() )
  {
    return false;
  }
  out = mPicture;
  picturesSeen = mPicturesDrawn;
  return true;
}

void EmulationThread::serveRequests()
{
  for ( ;; )
  {
    Pending pending;
    {
      std::scoped_lock const lock{ mMutex };
      if ( mPending.empty() )
      {
        return;
      }
      pending = std::move( mPending.front() );
      mPending.pop_front();
    }
    pending.answer.set_value( mDispatcher.handle( pending.request ) );
  }
}

void EmulationThread::runFrame( machine::Machine& machine )
{
  machine.setHostInputs( unpackInputs( mHostInputs ) );
  machine.runFrames( 1 );
  if ( mAudio != nullptr )
  {
    mAudio->push( machine.audio(), machine.audioRate() );
  }

  // Frames run only to be shown would stop at a breakpoint or a watchpoint,
  // or reach an audio capture, that the frame kept never did.
  int const ahead = mRunAhead;
  if ( ahead <= 0 || !machine.breakpoints().empty() || !machine.watchpoints().empty() || machine.audioListened() )
  {
    if ( machine.picturesDrawn() != mMachinePictures )
    {
      publishPicture( machine );
    }
    keep( mRewindKept ? machine.saveState() : std::vector<std::uint8_t>{} );
    return;
  }

  // On with the same controls held, the last of those frames shown and their
  // sound dropped; then back to the frame kept, a state the history keeps too.
  std::vector<std::uint8_t> state = machine.saveState();
  machine.runFrames( ahead );
  publishPicture( machine );
  machine.loadState( state );
  mMachinePictures = machine.picturesDrawn();
  keep( std::move( state ) );
}

void EmulationThread::keep( std::vector<std::uint8_t> state )
{
  checkHistory();
  if ( !mRewindKept )
  {
    mHistory.clear();
    return;
  }
  if ( mHistory.size() == HISTORY_FRAMES )
  {
    mHistory.pop_front();
  }
  mHistory.push_back( std::move( state ) );
}

void EmulationThread::stepBack( machine::Machine& machine )
{
  checkHistory();
  // The latest state is the frame shown; the first step back is to the one
  // before it. The oldest is kept, to be stayed at.
  if ( mHistory.size() < 2 || !mRewindKept )
  {
    return;
  }
  mHistory.pop_back();
  if ( machine.loadState( mHistory.back() ) )
  {
    publishPicture( machine );
  }
}

void EmulationThread::checkHistory()
{
  auto of = std::pair{ mEmulator.gameName(), mEmulator.region() };
  if ( of != mHistoryOf )
  {
    mHistory.clear();
    mHistoryOf = std::move( of );
  }
}

void EmulationThread::publishPicture( machine::Machine const& machine )
{
  mMachinePictures = machine.picturesDrawn();
  auto const picture = machine.picture();
  std::scoped_lock const lock{ mPictureMutex };
  mPicture.assign( picture.begin(), picture.end() );
  ++mPicturesDrawn;
}

void EmulationThread::loop()
{
  using Clock = std::chrono::steady_clock;
  auto last = Clock::now();
  double owed = 0.0;
  for ( ;; )
  {
    serveRequests();
    {
      std::scoped_lock const lock{ mMutex };
      if ( mStop )
      {
        break;
      }
    }

    // The host's clock paces the emulation, so that it runs at the board's
    // speed whatever the display's refresh rate; the audio device's clock
    // drifts from it, which AudioOutput absorbs.
    auto const now = Clock::now();
    double const elapsed = std::chrono::duration<double>( now - last ).count();
    last = now;
    machine::Machine* const machine = mEmulator.machine();
    auto wakeAt = now + IDLE_WAIT;
    bool const rewinding = mRewinding;
    if ( machine == nullptr || ( mPaused && !rewinding ) )
    {
      owed = 0.0;
    }
    else
    {
      owed = std::min( owed + ( elapsed * FRAMES_PER_SECOND ), MAX_FRAMES_OWED );
      if ( owed >= 1.0 )
      {
        owed -= 1.0;
        if ( rewinding )
        {
          stepBack( *machine );
        }
        else
        {
          runFrame( *machine );
        }
        continue;
      }
      wakeAt = now + std::chrono::duration_cast<Clock::duration>(
                         std::chrono::duration<double>( ( 1.0 - owed ) / FRAMES_PER_SECOND ) );
    }

    std::unique_lock lock{ mMutex };
    mWake.wait_until( lock, wakeAt, [this] { return mStop || !mPending.empty(); } );
  }

  // Whoever is still waiting is told why no answer comes.
  std::scoped_lock const lock{ mMutex };
  for ( Pending& pending : mPending )
  {
    pending.answer.set_value(
        control::Json{ { "id", 0 },
                       { "ok", false },
                       { "error", { { "code", "shutting_down" }, { "message", "The emulator is closing" } } } } );
  }
  mPending.clear();
}

} // namespace pgm::app
