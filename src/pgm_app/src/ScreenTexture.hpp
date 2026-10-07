#pragma once

#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <span>

namespace pgm::app
{

/// The GPU texture a frame of the emulated screen is shown from. It owns the
/// texture and the transfer buffer frames are staged through, so that showing
/// a frame allocates nothing.
class ScreenTexture
{
public:
  /// Creates both on `device`. Fails, with SDL's reason in SDL_GetError(),
  /// where the device cannot provide them.
  explicit ScreenTexture( SDL_GPUDevice* device );
  ~ScreenTexture();

  ScreenTexture( ScreenTexture const& ) = delete;
  ScreenTexture& operator=( ScreenTexture const& ) = delete;
  ScreenTexture( ScreenTexture&& ) = delete;
  ScreenTexture& operator=( ScreenTexture&& ) = delete;

  [[nodiscard]] bool valid() const;

  /// Queues `rgba`, one frame of RGBA pixels, to replace the texture's content
  /// when `commands` executes. Must be recorded outside any render pass.
  void upload( SDL_GPUCommandBuffer* commands, std::span<std::uint8_t const> rgba );

  [[nodiscard]] SDL_GPUTexture* texture() const;

private:
  SDL_GPUDevice* mDevice;
  SDL_GPUTexture* mTexture{};
  SDL_GPUTransferBuffer* mTransfer{};
};

} // namespace pgm::app
