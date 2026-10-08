#pragma once

#include "AudioOutput.hpp"

#include "pgm/Emulator.hpp"
#include "pgm/control/Dispatcher.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace pgm::app
{

/// The emulator on a thread of its own (docs/architecture.md). It runs the
/// machine at the board's speed, paced by the host's clock, and answers
/// requests between two frames, so that the interface and every transport
/// attached to it drive the one machine without touching it. Nothing outside
/// reaches the emulator but through handle().
class EmulationThread
{
public:
  /// Starts the thread, with no game loaded. `audio`, if not null, is fed the
  /// sound, and must outlive the thread.
  EmulationThread( Settings settings, AudioOutput* audio );
  ~EmulationThread();

  EmulationThread( EmulationThread const& ) = delete;
  EmulationThread& operator=( EmulationThread const& ) = delete;
  EmulationThread( EmulationThread&& ) = delete;
  EmulationThread& operator=( EmulationThread&& ) = delete;

  /// Answers a request of the control protocol, on the emulation thread
  /// between two frames; waits for the answer. Safe from any thread.
  [[nodiscard]] control::Json handle( control::Json const& request );

  /// The dispatcher's methods, which do not change once it is made.
  [[nodiscard]] std::vector<control::Method> const& methods() const;

  /// Stops running frames, or starts again; requests are answered either way.
  void setPaused( bool paused );

  /// The buttons the keyboard and the gamepads hold, PGM.sv's IN0..IN3,
  /// applied before each frame.
  void setHostInputs( std::array<std::uint16_t, 4> const& pressed );

  /// Whether the last seconds are kept, a state a frame, to be rewound
  /// through (docs/decisions/0018-rewind-keeps-every-frame.md).
  void setRewindKept( bool kept );
  /// While `held`, each frame goes a frame back instead of on, silently, paused
  /// or not; let go, the game runs on from there.
  void setRewinding( bool held );

  /// How many frames past each frame run are run and shown, then taken back,
  /// so that a control is seen sooner; 0 for none
  /// (docs/decisions/0019-run-ahead.md).
  void setRunAhead( int frames );

  /// Copies the last complete picture into `out` if it is newer than
  /// `picturesSeen`, and updates that; false if there is none newer.
  bool takePicture( std::int64_t& picturesSeen, std::vector<std::uint8_t>& out );

private:
  struct Pending
  {
    control::Json request;
    std::promise<control::Json> answer;
  };

  void loop();
  /// Answers every request waiting, on the emulation thread.
  void serveRequests();
  /// Runs one frame and hands its sound and picture on, and keeps its state;
  /// with run-ahead, the picture is that of the frames run past it.
  void runFrame( machine::Machine& machine );
  /// Keeps `state` as the latest of the history, while one is kept.
  void keep( std::vector<std::uint8_t> state );
  /// Goes back to the frame before the last kept, and hands its picture on.
  void stepBack( machine::Machine& machine );
  /// Forgets the states kept when they are not for the cartridge running:
  /// another game or region has other ROMs, which a state does not hold.
  void checkHistory();
  /// Hands the machine's picture on to takePicture().
  void publishPicture( machine::Machine const& machine );

  Emulator mEmulator;
  control::Dispatcher mDispatcher;
  std::vector<control::Method> const mMethods;
  AudioOutput* mAudio;

  std::mutex mMutex;
  std::condition_variable mWake;
  std::deque<Pending> mPending;
  bool mStop{};

  std::atomic<bool> mPaused{};
  std::atomic<std::uint64_t> mHostInputs{};
  std::atomic<bool> mRewindKept{ true };
  std::atomic<bool> mRewinding{};
  std::atomic<int> mRunAhead{};

  /// The states after the last frames run, the latest last, and the game and
  /// region they are of.
  std::deque<std::vector<std::uint8_t>> mHistory;
  std::pair<std::optional<std::string>, std::optional<std::string>> mHistoryOf;
  /// The machine's count of pictures when one was last handed on.
  std::int64_t mMachinePictures{ -1 };

  std::mutex mPictureMutex;
  std::vector<std::uint8_t> mPicture;
  /// Pictures handed on: the machine's own count goes back with a rewind.
  std::int64_t mPicturesDrawn{ -1 };

  std::thread mThread;
};

} // namespace pgm::app
