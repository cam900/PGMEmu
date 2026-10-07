#pragma once

#include "ScreenTexture.hpp"

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
  /// any of them cannot be had.
  static std::expected<std::unique_ptr<Application>, std::string> create();

  ~Application();

  Application( Application const& ) = delete;
  Application& operator=( Application const& ) = delete;
  Application( Application&& ) = delete;
  Application& operator=( Application&& ) = delete;

  /// Runs until the window is closed or Quit is chosen.
  void run();

private:
  Application( SDL_Window* window, SDL_GPUDevice* device );

  /// Lays out one frame of the user interface.
  void drawInterface();
  void drawMenuBar();
  void drawScreenWindow();
  void drawStatusWindow();

  /// Uploads what changed, then renders the interface into the swapchain.
  void renderFrame();

  SDL_Window* mWindow;
  SDL_GPUDevice* mDevice;
  std::unique_ptr<ScreenTexture> mScreen;
  std::vector<std::uint8_t> mFrame;
  bool mFrameChanged{ true };
  control::Dispatcher mDispatcher;
  std::string mImguiIniPath;
  bool mQuit{};
  bool mShowStatus{ true };
  bool mShowImguiDemo{};
};

} // namespace pgm::app
