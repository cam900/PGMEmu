#pragma once

#include "GpuTexture.hpp"

#include "pgm/control/Dispatcher.hpp"

#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace pgm::app
{

/// The video debugger: which layers are drawn, IGS023's registers, the sprite
/// list, and viewers of the tiles and the tile maps. It reads everything
/// through the control protocol's video methods, as an agent would
/// (docs/decisions/0005-one-control-api.md), and asks again at most twice a
/// second while a tab is shown.
class VideoWindow
{
public:
  /// Answers a request of the control protocol: `method` with `params`.
  using Request = std::function<control::Json( std::string const& method, control::Json params )>;

  VideoWindow( SDL_GPUDevice* device, Request request );

  /// Lays the window out while `open`, which its close button clears.
  void draw( bool& open );

  /// Uploads the images fetched since the last call. Must be recorded outside
  /// any render pass.
  void upload( SDL_GPUCommandBuffer* commands );

private:
  /// An image a tab shows, and the RGBA fetched for it but not yet uploaded.
  struct View
  {
    std::unique_ptr<GpuTexture> texture;
    std::vector<std::uint8_t> pending;
    std::uint32_t pendingWidth{};
    std::uint32_t pendingHeight{};
    double fetchedAt{ -1.0 };
  };

  void drawLayers();
  void drawSprites();
  void drawTiles();
  void drawTilemap();

  /// Whether it is time to ask again for what was last asked at `fetchedAt`.
  [[nodiscard]] static bool due( double fetchedAt );
  /// Asks for an image in RGBA and keeps it for the next upload.
  void fetch( View& view, std::string const& method, control::Json params );
  /// Shows `view`'s texture at `scale`, its pixels kept square and sharp.
  static void show( View const& view, float scale );

  SDL_GPUDevice* mDevice;
  Request mRequest;

  control::Json mLayers;
  control::Json mRegisters;
  double mLayersAt{ -1.0 };
  control::Json mSprites;
  double mSpritesAt{ -1.0 };

  int mTileLayer{};
  int mTileFirst{};
  int mTilePalette{};
  float mTileScale{ 2.0F };
  View mTiles;
  int mMapLayer{};
  float mMapScale{ 1.0F };
  View mMap;
};

} // namespace pgm::app
