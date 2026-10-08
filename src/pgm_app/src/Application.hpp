#pragma once

#include "AudioOutput.hpp"
#include "EmulationThread.hpp"
#include "GpuTexture.hpp"
#include "VideoWindow.hpp"

#include "pgm/Emulator.hpp"
#include "pgm/control/Dispatcher.hpp"
#include "pgm/server/McpHttpServer.hpp"
#include "pgm/server/McpServer.hpp"
#include "pgm/server/TcpLineServer.hpp"

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

  /// Serves the control protocol to other programs while the window is open:
  /// JSON-lines on TCP `linePort`, MCP over HTTP on `mcpPort`, each unless 0.
  /// Their requests run on the emulation thread, on the machine on screen.
  /// Throws std::runtime_error when a port cannot be had.
  void serve( std::uint16_t linePort, std::uint16_t mcpPort );

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

  /// Answers a request of the control protocol, on the emulation thread.
  control::Json request( std::string const& method, control::Json params = control::Json::object() );

  /// Hands the keyboard to the emulation thread, and takes the last picture
  /// it completed.
  void updateEmulation();

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
  std::unique_ptr<GpuTexture> mScreen;
  std::vector<std::uint8_t> mFrame;
  bool mFrameChanged{ true };
  /// Null when no audio device could be opened: the emulation then runs
  /// silent, paced by the clock alone.
  std::unique_ptr<AudioOutput> mAudio;
  std::unique_ptr<EmulationThread> mEmulation;
  std::unique_ptr<server::TcpLineServer> mLineServer;
  std::unique_ptr<server::McpServer> mMcp;
  std::unique_ptr<server::McpHttpServer> mMcpHttp;
  std::unique_ptr<VideoWindow> mVideo;
  std::string mImguiIniPath;
  bool mQuit{};
  bool mShowStatus{ true };
  bool mShowSound{};
  bool mShowVideo{};
  bool mPaused{};
  /// Whether the screen window had focus when the interface was last drawn.
  bool mScreenFocused{};
  std::int64_t mPicturesShown{ -1 };
  std::string mLastError;
  bool mShowImguiDemo{};
};

} // namespace pgm::app
