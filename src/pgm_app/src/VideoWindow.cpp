#include "VideoWindow.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace pgm::app
{

namespace
{

constexpr char const* WINDOW = "Video";
constexpr double REFRESH_SECONDS = 0.5;
constexpr std::array<char const*, 2> LAYERS{ "text", "background" };

/// The bytes of standard base64 text, as the protocol encodes images.
std::vector<std::uint8_t> decodeBase64( std::string_view text )
{
  auto const value = []( char c ) -> int
  {
    if ( c >= 'A' && c <= 'Z' )
    {
      return c - 'A';
    }
    if ( c >= 'a' && c <= 'z' )
    {
      return c - 'a' + 26;
    }
    if ( c >= '0' && c <= '9' )
    {
      return c - '0' + 52;
    }
    if ( c == '+' )
    {
      return 62;
    }
    return c == '/' ? 63 : -1;
  };
  std::vector<std::uint8_t> bytes;
  bytes.reserve( ( text.size() / 4 ) * 3 );
  std::uint32_t bits = 0;
  int count = 0;
  for ( char const c : text )
  {
    int const v = value( c );
    if ( v < 0 )
    {
      continue;
    }
    bits = ( bits << 6U ) | static_cast<std::uint32_t>( v );
    count += 6;
    if ( count >= 8 )
    {
      count -= 8;
      bytes.push_back( static_cast<std::uint8_t>( bits >> static_cast<unsigned>( count ) ) );
    }
  }
  return bytes;
}

bool succeeded( control::Json const& response )
{
  return response.at( "ok" ) == true;
}

} // namespace

VideoWindow::VideoWindow( SDL_GPUDevice* device, Request request ) : mDevice{ device }, mRequest{ std::move( request ) }
{
}

bool VideoWindow::due( double fetchedAt )
{
  return fetchedAt < 0.0 || ImGui::GetTime() - fetchedAt >= REFRESH_SECONDS;
}

void VideoWindow::draw( bool& open )
{
  if ( !open )
  {
    return;
  }
  if ( ImGui::Begin( WINDOW, &open ) && ImGui::BeginTabBar( "video" ) )
  {
    if ( ImGui::BeginTabItem( "Layers" ) )
    {
      drawLayers();
      ImGui::EndTabItem();
    }
    if ( ImGui::BeginTabItem( "Sprites" ) )
    {
      drawSprites();
      ImGui::EndTabItem();
    }
    if ( ImGui::BeginTabItem( "Tiles" ) )
    {
      drawTiles();
      ImGui::EndTabItem();
    }
    if ( ImGui::BeginTabItem( "Tile map" ) )
    {
      drawTilemap();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

void VideoWindow::drawLayers()
{
  if ( due( mLayersAt ) )
  {
    auto const layers = mRequest( "video.layers", control::Json::object() );
    auto const registers = mRequest( "video.registers", control::Json::object() );
    mLayers = succeeded( layers ) ? layers.at( "result" ) : control::Json{};
    mRegisters = succeeded( registers ) ? registers.at( "result" ) : control::Json{};
    mLayersAt = ImGui::GetTime();
  }
  if ( mLayers.is_null() )
  {
    ImGui::TextUnformatted( "No game loaded" );
    return;
  }

  ImGui::TextUnformatted( "Draw the picture with:" );
  for ( char const* layer : { "text", "background", "sprites" } )
  {
    bool shown = mLayers.at( layer ).get<bool>();
    if ( ImGui::Checkbox( layer, &shown ) )
    {
      auto const response = mRequest( "video.layers", control::Json{ { layer, shown } } );
      if ( succeeded( response ) )
      {
        mLayers = response.at( "result" );
      }
    }
  }

  if ( !mRegisters.is_null() )
  {
    ImGui::SeparatorText( "IGS023" );
    auto const& background = mRegisters.at( "background_scroll" );
    auto const& text = mRegisters.at( "text_scroll" );
    ImGui::Text(
        "Background scroll  x %4u  y %4u", background.at( "x" ).get<unsigned>(), background.at( "y" ).get<unsigned>() );
    ImGui::Text( "Text scroll        x %4u  y %4u", text.at( "x" ).get<unsigned>(), text.at( "y" ).get<unsigned>() );
    ImGui::Text( "Line counter %u, flags %04x",
                 mRegisters.at( "line_counter" ).get<unsigned>(),
                 mRegisters.at( "flags" ).get<unsigned>() );
    auto const& registers = mRegisters.at( "registers" );
    for ( std::size_t i = 0; i < registers.size(); ++i )
    {
      ImGui::Text( "%02zx: %04x", i * 2, registers.at( i ).get<unsigned>() );
      if ( i % 4 != 3 )
      {
        ImGui::SameLine();
      }
    }
  }
}

void VideoWindow::drawSprites()
{
  if ( due( mSpritesAt ) )
  {
    auto const response = mRequest( "video.sprites", control::Json::object() );
    mSprites = succeeded( response ) ? response.at( "result" ) : control::Json{};
    mSpritesAt = ImGui::GetTime();
  }
  if ( mSprites.is_null() )
  {
    ImGui::TextUnformatted( "No game loaded" );
    return;
  }

  ImGui::Text( "%u sprites in the list DMA last copied", mSprites.at( "count" ).get<unsigned>() );
  ImGuiTableFlags const flags =
      ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
  if ( ImGui::BeginTable( "sprites", 9, flags ) )
  {
    ImGui::TableSetupScrollFreeze( 0, 1 );
    for ( char const* heading : { "#", "x", "y", "size", "palette", "priority", "flip", "zoom", "masks" } )
    {
      ImGui::TableSetupColumn( heading );
    }
    ImGui::TableHeadersRow();
    int index = 0;
    for ( auto const& sprite : mSprites.at( "sprites" ) )
    {
      auto const number = [&sprite]( char const* name ) { return sprite.at( name ).get<unsigned>(); };
      auto const flag = [&sprite]( char const* name ) { return sprite.at( name ).get<bool>(); };
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text( "%3d", index++ );
      ImGui::TableNextColumn();
      ImGui::Text( "%4u", number( "x" ) );
      ImGui::TableNextColumn();
      ImGui::Text( "%4u", number( "y" ) );
      ImGui::TableNextColumn();
      ImGui::Text( "%ux%u", number( "width" ) * 16, number( "height" ) );
      ImGui::TableNextColumn();
      ImGui::Text( "%2u", number( "palette" ) );
      ImGui::TableNextColumn();
      ImGui::TextUnformatted( flag( "low_priority" ) ? "low" : "high" );
      ImGui::TableNextColumn();
      ImGui::Text( "%s%s", flag( "flip_x" ) ? "x" : "-", flag( "flip_y" ) ? "y" : "-" );
      ImGui::TableNextColumn();
      ImGui::Text( "%2u %2u", number( "scale_x" ), number( "scale_y" ) );
      ImGui::TableNextColumn();
      ImGui::Text( "%06x", number( "mask_address" ) );
    }
    ImGui::EndTable();
  }
}

void VideoWindow::drawTiles()
{
  bool changed = ImGui::Combo( "Layer", &mTileLayer, LAYERS.data(), static_cast<int>( LAYERS.size() ) );
  changed |= ImGui::InputInt( "First tile", &mTileFirst, 256, 4096, ImGuiInputTextFlags_CharsHexadecimal );
  changed |= ImGui::SliderInt( "Palette", &mTilePalette, 0, 31 );
  ImGui::SliderFloat( "Zoom", &mTileScale, 1.0F, 4.0F, "%.0f" );
  mTileFirst = std::max( mTileFirst, 0 );
  if ( changed || due( mTiles.fetchedAt ) )
  {
    bool const text = mTileLayer == 0;
    fetch( mTiles,
           "video.tiles",
           control::Json{ { "layer", LAYERS.at( static_cast<std::size_t>( mTileLayer ) ) },
                          { "first", mTileFirst },
                          { "count", text ? 512 : 64 },
                          { "columns", text ? 32 : 8 },
                          { "palette", mTilePalette } } );
  }
  show( mTiles, mTileScale );
}

void VideoWindow::drawTilemap()
{
  bool const changed = ImGui::Combo( "Layer", &mMapLayer, LAYERS.data(), static_cast<int>( LAYERS.size() ) );
  ImGui::SliderFloat( "Zoom", &mMapScale, 0.25F, 4.0F, "%.2f" );
  if ( changed || due( mMap.fetchedAt ) )
  {
    fetch( mMap, "video.tilemap", control::Json{ { "layer", LAYERS.at( static_cast<std::size_t>( mMapLayer ) ) } } );
  }
  show( mMap, mMapScale );
}

void VideoWindow::fetch( View& view, std::string const& method, control::Json params )
{
  params["format"] = "rgba";
  auto const response = mRequest( method, std::move( params ) );
  view.fetchedAt = ImGui::GetTime();
  if ( !succeeded( response ) )
  {
    return;
  }
  auto const& result = response.at( "result" );
  view.pendingWidth = result.at( "width" ).get<std::uint32_t>();
  view.pendingHeight = result.at( "height" ).get<std::uint32_t>();
  view.pending = decodeBase64( result.at( "rgba_base64" ).get_ref<std::string const&>() );
}

void VideoWindow::show( View const& view, float scale )
{
  if ( !view.texture )
  {
    ImGui::TextUnformatted( "No game loaded" );
    return;
  }
  ImGui::BeginChild( "image", ImVec2{ 0.0F, 0.0F }, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar );
  ImDrawList* const drawList = ImGui::GetWindowDrawList();
  ImGuiPlatformIO const& platform = ImGui::GetPlatformIO();
  drawList->AddCallback( platform.DrawCallback_SetSamplerNearest, nullptr );
  ImGui::Image( ImTextureRef{ reinterpret_cast<ImTextureID>( view.texture->texture() ) },
                ImVec2{ static_cast<float>( view.texture->width() ) * scale,
                        static_cast<float>( view.texture->height() ) * scale } );
  drawList->AddCallback( platform.DrawCallback_SetSamplerLinear, nullptr );
  ImGui::EndChild();
}

void VideoWindow::upload( SDL_GPUCommandBuffer* commands )
{
  for ( View* view : { &mTiles, &mMap } )
  {
    if ( view->pending.empty() )
    {
      continue;
    }
    if ( !view->texture || view->texture->width() != view->pendingWidth ||
         view->texture->height() != view->pendingHeight )
    {
      view->texture = std::make_unique<GpuTexture>( mDevice, view->pendingWidth, view->pendingHeight );
    }
    if ( view->texture->valid() &&
         view->pending.size() == std::size_t{ view->pendingWidth } * std::size_t{ view->pendingHeight } * 4 )
    {
      view->texture->upload( commands, view->pending );
    }
    view->pending.clear();
  }
}

} // namespace pgm::app
