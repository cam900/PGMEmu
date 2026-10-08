#pragma once

#include "AudioOutput.hpp"
#include "ScreenTexture.hpp"

#include "pgm/Emulator.hpp"
#include "pgm/control/Dispatcher.hpp"

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_video.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <vector>

namespace pgm::app
{

/// The desktop frontend: an SDL3 window rendered through SDL_GPU, with Dear
/// ImGui docked over it (docs/decisions/0008-the-renderer-is-sdl-gpu.md). It
/// reaches the emulator only through the dispatcher, as every other client does.
class Application
{
public:
  /// Opens the window and the GPU device and sets up ImGui. Answers why when
  /// any of them cannot be had. `settings` say where games and the BIOS are.
  static std::expected<std::unique_ptr<Application>, std::string> create( Settings settings );

  /// Loads a game by set name or path, through the dispatcher, as an agent
  /// would; a failure is shown in the status window.
  void loadGame( std::string const& nameOrPath );

  ~Application();

  Application( Application const& ) = delete;
  Application& operator=( Application const& ) = delete;
  Application( Application&& ) = delete;
  Application& operator=( Application&& ) = delete;

  /// Runs until the window is closed or Quit is chosen.
  void run();

private:
  Application( SDL_Window* window, SDL_GPUDevice* device, Settings settings );

  /// Runs the machine for as many frames as the time since the last call
  /// holds, if a game is loaded and it is not paused; plays their sound and
  /// takes the last picture completed.
  void emulate();

  /// Lays out one frame of the user interface.
  void drawInterface();
  void drawMenuBar();
  void drawScreenWindow();
  void drawStatusWindow();
  void drawSoundWindow();

  /// Uploads what changed, then renders the interface into the swapchain.
  void renderFrame();

  SDL_Window* mWindow;
  SDL_GPUDevice* mDevice;
  std::unique_ptr<ScreenTexture> mScreen;
  std::vector<std::uint8_t> mFrame;
  bool mFrameChanged{ true };
  Emulator mEmulator;
  control::Dispatcher mDispatcher;
  /// Null when no audio device could be opened: the emulation then runs
  /// silent, paced by the clock alone.
  std::unique_ptr<AudioOutput> mAudio;
  std::uint64_t mLastTicksNs{};
  double mFramesOwed{};
  std::string mImguiIniPath;
  bool mQuit{};
  bool mShowStatus{ true };
  bool mShowSound{};
  bool mPaused{};
  /// Whether the screen window had focus when the interface was last drawn.
  bool mScreenFocused{};
  std::int64_t mPicturesShown{ -1 };
  std::string mLastError;
  bool mShowImguiDemo{};
};

} // namespace pgm::app
