#include "ScreenTexture.hpp"

#include "pgm/video/Screen.hpp"

#include <cassert>
#include <cstring>

namespace pgm::app
{

namespace
{

constexpr auto WIDTH = static_cast<std::uint32_t>( video::SCREEN_WIDTH );
constexpr auto HEIGHT = static_cast<std::uint32_t>( video::SCREEN_HEIGHT );
constexpr std::uint32_t FRAME_BYTES = WIDTH * HEIGHT * 4;

} // namespace

ScreenTexture::ScreenTexture( SDL_GPUDevice* device ) : mDevice{ device }
{
  SDL_GPUTextureCreateInfo textureInfo{};
  textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
  textureInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  textureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
  textureInfo.width = WIDTH;
  textureInfo.height = HEIGHT;
  textureInfo.layer_count_or_depth = 1;
  textureInfo.num_levels = 1;
  mTexture = SDL_CreateGPUTexture( mDevice, &textureInfo );

  SDL_GPUTransferBufferCreateInfo transferInfo{};
  transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  transferInfo.size = FRAME_BYTES;
  mTransfer = SDL_CreateGPUTransferBuffer( mDevice, &transferInfo );
}

ScreenTexture::~ScreenTexture()
{
  if ( mTransfer != nullptr )
  {
    SDL_ReleaseGPUTransferBuffer( mDevice, mTransfer );
  }
  if ( mTexture != nullptr )
  {
    SDL_ReleaseGPUTexture( mDevice, mTexture );
  }
}

bool ScreenTexture::valid() const
{
  return mTexture != nullptr && mTransfer != nullptr;
}

void ScreenTexture::upload( SDL_GPUCommandBuffer* commands, std::span<std::uint8_t const> rgba )
{
  assert( rgba.size() == FRAME_BYTES );

  // `cycle` lets the driver hand out a fresh buffer while the GPU may still be
  // reading the previous frame from this one, so the copy never waits on it.
  void* const staging = SDL_MapGPUTransferBuffer( mDevice, mTransfer, true );
  if ( staging == nullptr )
  {
    return;
  }
  std::memcpy( staging, rgba.data(), FRAME_BYTES );
  SDL_UnmapGPUTransferBuffer( mDevice, mTransfer );

  SDL_GPUTextureTransferInfo source{};
  source.transfer_buffer = mTransfer;
  source.pixels_per_row = WIDTH;
  source.rows_per_layer = HEIGHT;

  SDL_GPUTextureRegion destination{};
  destination.texture = mTexture;
  destination.w = WIDTH;
  destination.h = HEIGHT;
  destination.d = 1;

  SDL_GPUCopyPass* const copy = SDL_BeginGPUCopyPass( commands );
  SDL_UploadToGPUTexture( copy, &source, &destination, true );
  SDL_EndGPUCopyPass( copy );
}

SDL_GPUTexture* ScreenTexture::texture() const
{
  return mTexture;
}

} // namespace pgm::app
