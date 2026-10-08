#include "Application.hpp"

#include "Keyboard.hpp"
#include "TestPattern.hpp"

#include "pgm/video/Screen.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <utility>

namespace pgm::app
{

namespace
{

constexpr auto SCREEN_WIDTH = static_cast<float>( video::SCREEN_WIDTH );
constexpr auto SCREEN_HEIGHT = static_cast<float>( video::SCREEN_HEIGHT );

// The window opens at three times the screen, plus room for the menu bar and a
// side panel, scaled for the display it opens on.
constexpr int INITIAL_WIDTH = 1600;
constexpr int INITIAL_HEIGHT = 900;

constexpr char const* SCREEN_WINDOW = "Screen";
constexpr char const* STATUS_WINDOW = "Status";
constexpr char const* SOUND_WINDOW = "Sound";
constexpr char const* VIDEO_WINDOW = "Video";

std::string sdlError( std::string const& what )
{
  return what + ": " + SDL_GetError();
}

/// What the region codes of docs/spec/pgm-format.md §4.3 stand for.
constexpr std::array<std::pair<std::string_view, std::string_view>, 8> REGION_NAMES{ {
    { "WRLD", "World" },
    { "HGKG", "Hong Kong" },
    { "JAPN", "Japan" },
    { "KREA", "Korea" },
    { "TAWN", "Taiwan" },
    { "CHNA", "China" },
    { "USOA", "USA" },
    { "SNGP", "Singapore" },
} };

std::string_view regionName( std::string_view code )
{
  auto const name = std::ranges::find( REGION_NAMES, code, &std::pair<std::string_view, std::string_view>::first );
  return name == REGION_NAMES.end() ? code : name->second;
}

} // namespace

std::expected<std::unique_ptr<Application>, std::string> Application::create( Settings settings )
{
  if ( !SDL_Init( SDL_INIT_VIDEO | SDL_INIT_GAMEPAD ) )
  {
    return std::unexpected( sdlError( "SDL_Init" ) );
  }

  float const scale = SDL_GetDisplayContentScale( SDL_GetPrimaryDisplay() );
  SDL_Window* const window = SDL_CreateWindow( "PGMEmu",
                                               static_cast<int>( INITIAL_WIDTH * scale ),
                                               static_cast<int>( INITIAL_HEIGHT * scale ),
                                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY );
  if ( window == nullptr )
  {
    return std::unexpected( sdlError( "SDL_CreateWindow" ) );
  }

  // Every shader format the ImGui backend ships bytecode for, so that SDL may
  // pick the native API: Metal on macOS, Vulkan or D3D12 elsewhere.
  SDL_GPUDevice* const device = SDL_CreateGPUDevice( SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
                                                         SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB,
                                                     false,
                                                     nullptr );
  if ( device == nullptr )
  {
    SDL_DestroyWindow( window );
    return std::unexpected( sdlError( "SDL_CreateGPUDevice" ) );
  }
  if ( !SDL_ClaimWindowForGPUDevice( device, window ) )
  {
    SDL_DestroyGPUDevice( device );
    SDL_DestroyWindow( window );
    return std::unexpected( sdlError( "SDL_ClaimWindowForGPUDevice" ) );
  }
  SDL_SetGPUSwapchainParameters( device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC );

  // From here on the destructor owns the window and the device.
  std::unique_ptr<Application> application{ new Application{ window, device, std::move( settings ) } };
  if ( !application->mScreen->valid() )
  {
    return std::unexpected( sdlError( "creating the screen texture" ) );
  }
  return application;
}

Application::Application( SDL_Window* window, SDL_GPUDevice* device, Settings settings )
    : mWindow{ window }, mDevice{ device },
      mScreen{ std::make_unique<GpuTexture>( device, video::SCREEN_WIDTH, video::SCREEN_HEIGHT ) },
      mFrame{ makeTestPattern() }, mAudio{ AudioOutput::open() },
      mEmulation{ std::make_unique<EmulationThread>( std::move( settings ), mAudio.get() ) },
      mVideo{ std::make_unique<VideoWindow>( device,
                                             [this]( std::string const& method, control::Json params )
                                             { return request( method, std::move( params ) ); } ) }
{
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;

  // The layout is kept with the user's preferences rather than in whatever
  // directory the application was started from.
  if ( char* const prefPath = SDL_GetPrefPath( "PGMEmu", "pgmemu" ); prefPath != nullptr )
  {
    mImguiIniPath = std::string{ prefPath } + "imgui.ini";
    SDL_free( prefPath );
    io.IniFilename = mImguiIniPath.c_str();
  }
  else
  {
    io.IniFilename = nullptr;
  }

  float const scale = SDL_GetDisplayContentScale( SDL_GetPrimaryDisplay() );
  ImGui::StyleColorsDark();
  ImGui::GetStyle().ScaleAllSizes( scale );
  ImGui::GetStyle().FontScaleDpi = scale;

  ImGui_ImplSDL3_InitForSDLGPU( mWindow );
  ImGui_ImplSDLGPU3_InitInfo initInfo{};
  initInfo.Device = mDevice;
  initInfo.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat( mDevice, mWindow );
  initInfo.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
  ImGui_ImplSDLGPU3_Init( &initInfo );
}

Application::~Application()
{
  SDL_WaitForGPUIdle( mDevice );
  ImGui_ImplSDL3_Shutdown();
  ImGui_ImplSDLGPU3_Shutdown();
  ImGui::DestroyContext();
  mScreen.reset();
  mVideo.reset();
  // The servers hand requests to the emulation thread, which feeds the audio
  // stream: they go in that order.
  mMcpHttp.reset();
  mMcp.reset();
  mLineServer.reset();
  mEmulation.reset();
  mAudio.reset();
  SDL_ReleaseWindowFromGPUDevice( mDevice, mWindow );
  SDL_DestroyGPUDevice( mDevice );
  SDL_DestroyWindow( mWindow );
  SDL_Quit();
}

void Application::run()
{
  while ( !mQuit )
  {
    SDL_Event event;
    while ( SDL_PollEvent( &event ) )
    {
      ImGui_ImplSDL3_ProcessEvent( &event );
      if ( event.type == SDL_EVENT_QUIT ||
           ( event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID( mWindow ) ) )
      {
        mQuit = true;
      }
    }

    // A minimised window has no swapchain to present to; waiting here keeps
    // the loop from spinning until it is restored.
    if ( ( SDL_GetWindowFlags( mWindow ) & SDL_WINDOW_MINIMIZED ) != 0 )
    {
      SDL_Delay( 10 );
      continue;
    }

    updateEmulation();

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    drawInterface();
    ImGui::Render();
    renderFrame();
  }
}

control::Json Application::request( std::string const& method, control::Json params )
{
  return mEmulation->handle( control::Json{ { "id", 1 }, { "method", method }, { "params", std::move( params ) } } );
}

void Application::serve( std::uint16_t linePort, std::uint16_t mcpPort )
{
  auto const handler = [this]( control::Json const& request ) { return mEmulation->handle( request ); };
  if ( linePort != 0 )
  {
    mLineServer = std::make_unique<server::TcpLineServer>( linePort, handler );
    spdlog::info( "serving JSON-lines on 127.0.0.1:{}", mLineServer->port() );
  }
  if ( mcpPort != 0 )
  {
    mMcp = std::make_unique<server::McpServer>( mEmulation->methods(), handler );
    mMcpHttp = std::make_unique<server::McpHttpServer>( mcpPort, *mMcp );
    spdlog::info( "serving MCP on http://127.0.0.1:{}/mcp", mMcpHttp->port() );
  }
}

void Application::loadGame( std::string const& nameOrPath )
{
  bool const isPath = nameOrPath.find_first_of( "/.\\" ) != std::string::npos;
  auto const response = request( "emu.load_game", { { isPath ? "path" : "name", nameOrPath } } );
  mLastError = response.at( "ok" ) == true ? std::string{} : response.at( "error" ).at( "message" ).get<std::string>();
}

void Application::updateEmulation()
{
  // The game has the keyboard while its screen has focus, or nothing of the
  // interface does. ImGui's keyboard navigation asks for the keyboard whenever
  // any of its windows has focus, the screen's included, so its request alone
  // cannot decide. A text field being edited keeps it.
  ImGuiIO const& io = ImGui::GetIO();
  bool const toGame = !io.WantTextInput && ( mScreenFocused || !io.WantCaptureKeyboard );
  mEmulation->setKeyboard( toGame ? inputsFromKeyboard( SDL_GetKeyboardState( nullptr ) )
                                  : std::array<std::uint16_t, 4>{} );
  if ( mEmulation->takePicture( mPicturesShown, mFrame ) )
  {
    mFrameChanged = true;
  }
}

void Application::drawInterface()
{
  drawMenuBar();
  ImGuiID const dockspace = ImGui::DockSpaceOverViewport();

  // On the first run, before imgui.ini has a layout to restore, the screen
  // fills the dockspace and the status panel floats over it.
  ImGui::SetNextWindowDockID( dockspace, ImGuiCond_FirstUseEver );
  drawScreenWindow();
  drawStatusWindow();
  drawSoundWindow();
  mVideo->draw( mShowVideo );

  if ( mShowImguiDemo )
  {
    ImGui::ShowDemoWindow( &mShowImguiDemo );
  }
}

void Application::drawMenuBar()
{
  if ( !ImGui::BeginMainMenuBar() )
  {
    return;
  }
  if ( ImGui::BeginMenu( "File" ) )
  {
    if ( ImGui::MenuItem( "Quit" ) )
    {
      mQuit = true;
    }
    ImGui::EndMenu();
  }
  if ( ImGui::BeginMenu( "Emulation" ) )
  {
    if ( ImGui::MenuItem( "Pause", nullptr, &mPaused ) )
    {
      mEmulation->setPaused( mPaused );
    }
    if ( ImGui::MenuItem( "Reset" ) )
    {
      static_cast<void>( request( "emu.reset", { { "cycles", 100 } } ) );
    }
    if ( ImGui::BeginMenu( "Region" ) )
    {
      drawRegionMenu();
      ImGui::EndMenu();
    }
    ImGui::EndMenu();
  }
  if ( ImGui::BeginMenu( "View" ) )
  {
    ImGui::MenuItem( STATUS_WINDOW, nullptr, &mShowStatus );
    ImGui::MenuItem( SOUND_WINDOW, nullptr, &mShowSound );
    ImGui::MenuItem( VIDEO_WINDOW, nullptr, &mShowVideo );
    ImGui::Separator();
    ImGui::MenuItem( "ImGui demo", nullptr, &mShowImguiDemo );
    ImGui::EndMenu();
  }
  ImGui::EndMainMenuBar();
}

void Application::drawRegionMenu()
{
  auto const info = request( "emu.cartridge_info" );
  if ( info.at( "ok" ) != true || info.at( "result" ).at( "region_info" ).is_null() )
  {
    ImGui::TextDisabled( "This game has no regions" );
    return;
  }
  auto const status = request( "emu.status" );
  control::Json const current = status.at( "ok" ) == true ? status.at( "result" ).at( "region" ) : control::Json{};
  for ( control::Json const& region : info.at( "result" ).at( "region_info" ).at( "regions" ) )
  {
    auto const code = region.at( "id" ).get<std::string>();
    if ( ImGui::MenuItem( fmt::format( "{} ({})", regionName( code ), code ).c_str(), nullptr, current == code ) &&
         current != code )
    {
      auto const response = request( "emu.set_region", { { "region", code } } );
      mLastError =
          response.at( "ok" ) == true ? std::string{} : response.at( "error" ).at( "message" ).get<std::string>();
    }
  }
}

void Application::drawScreenWindow()
{
  ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2{ 0.0F, 0.0F } );
  bool const open = ImGui::Begin( SCREEN_WINDOW, nullptr, ImGuiWindowFlags_NoScrollbar );
  ImGui::PopStyleVar();
  mScreenFocused = ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows );
  if ( open )
  {
    // The largest whole multiple of the screen that fits, centred: a whole
    // multiple keeps every emulated pixel the same size.
    ImVec2 const available = ImGui::GetContentRegionAvail();
    float const scale =
        std::max( 1.0F, std::floor( std::min( available.x / SCREEN_WIDTH, available.y / SCREEN_HEIGHT ) ) );
    ImVec2 const size{ SCREEN_WIDTH * scale, SCREEN_HEIGHT * scale };
    ImVec2 const cursor = ImGui::GetCursorPos();
    ImGui::SetCursorPos( ImVec2{ cursor.x + std::max( 0.0F, ( available.x - size.x ) / 2.0F ),
                                 cursor.y + std::max( 0.0F, ( available.y - size.y ) / 2.0F ) } );

    // Sampled nearest, so that magnification copies pixels instead of blending
    // them; the sampler is restored for the rest of the interface.
    ImDrawList* const drawList = ImGui::GetWindowDrawList();
    ImGuiPlatformIO const& platform = ImGui::GetPlatformIO();
    drawList->AddCallback( platform.DrawCallback_SetSamplerNearest, nullptr );
    ImGui::Image( ImTextureRef{ reinterpret_cast<ImTextureID>( mScreen->texture() ) }, size );
    drawList->AddCallback( platform.DrawCallback_SetSamplerLinear, nullptr );
  }
  ImGui::End();
}

void Application::drawStatusWindow()
{
  if ( !mShowStatus )
  {
    return;
  }
  if ( ImGui::Begin( STATUS_WINDOW, &mShowStatus ) )
  {
    // Asked through the dispatcher, as an agent would ask, so that what the
    // window shows is what the protocol answers.
    auto const response = request( "emu.status" );
    if ( response.at( "ok" ) == true )
    {
      ImGui::Text( "Version: %s", response.at( "result" ).at( "version" ).get_ref<std::string const&>().c_str() );
    }
    else
    {
      ImGui::Text( "emu.status failed: %s", response.at( "error" ).dump().c_str() );
    }
    if ( response.at( "ok" ) == true && response.at( "result" ).contains( "frame" ) )
    {
      auto const& result = response.at( "result" );
      ImGui::Text( "Game: %s", result.at( "game_name" ).get_ref<std::string const&>().c_str() );
      ImGui::Text( "Frame: %lld", static_cast<long long>( result.at( "frame" ).get<std::int64_t>() ) );
    }
    else
    {
      ImGui::TextUnformatted( "No game loaded" );
    }
    if ( !mLastError.empty() )
    {
      ImGui::TextWrapped( "Load failed: %s", mLastError.c_str() );
    }
    ImGui::Text( "Interface: %.1f fps", static_cast<double>( ImGui::GetIO().Framerate ) );
  }
  ImGui::End();
}

void Application::drawSoundWindow()
{
  if ( !mShowSound )
  {
    return;
  }
  if ( ImGui::Begin( SOUND_WINDOW, &mShowSound ) )
  {
    ImGui::Text( "Output: %s", mAudio ? "default device" : "none" );
    if ( mAudio )
    {
      ImGui::SameLine();
      ImGui::Text( "(%.0f ms queued)", mAudio->queuedSeconds() * 1000.0 );
    }

    // The voices, as `audio.voices` reports them.
    auto const response = request( "audio.voices" );
    if ( response.at( "ok" ) != true )
    {
      ImGui::TextUnformatted( "No game loaded" );
      ImGui::End();
      return;
    }
    auto const& voices = response.at( "result" ).at( "voices" );
    ImGuiTableFlags const flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_SizingFixedFit;
    if ( ImGui::BeginTable( "voices", 9, flags ) )
    {
      ImGui::TableSetupScrollFreeze( 0, 1 );
      for ( char const* heading : { "#", "conf", "ctl", "fc", "address", "end", "level", "pan", "vctrl" } )
      {
        ImGui::TableSetupColumn( heading );
      }
      ImGui::TableHeadersRow();
      int index = 0;
      for ( auto const& voice : voices )
      {
        auto const field = [&voice]( char const* name ) { return voice.at( name ).get<unsigned>(); };
        // A voice is heard while its oscillator runs and its envelope is above
        // the bottom of the volume table.
        bool const sounding = ( field( "osc_ctl" ) & 2U ) == 0 && ( field( "vol_acc" ) >> 14U ) > 0x100;
        ImGui::TableNextRow();
        ImGui::BeginDisabled( !sounding );
        ImGui::TableNextColumn();
        ImGui::Text( "%2d", index++ );
        ImGui::TableNextColumn();
        ImGui::Text( "%02x", field( "osc_conf" ) );
        ImGui::TableNextColumn();
        ImGui::Text( "%02x", field( "osc_ctl" ) );
        ImGui::TableNextColumn();
        ImGui::Text( "%04x", field( "osc_fc" ) );
        ImGui::TableNextColumn();
        ImGui::Text( "%x:%05x", field( "osc_saddr" ) & 0xfU, field( "osc_acc" ) >> 9U );
        ImGui::TableNextColumn();
        ImGui::Text( "%05x", field( "osc_end" ) >> 9U );
        ImGui::TableNextColumn();
        ImGui::Text( "%03x", field( "vol_acc" ) >> 14U );
        ImGui::TableNextColumn();
        ImGui::Text( "%02x", field( "vol_pan" ) );
        ImGui::TableNextColumn();
        ImGui::Text( "%02x", field( "vol_ctrl" ) );
        ImGui::EndDisabled();
      }
      ImGui::EndTable();
    }
  }
  ImGui::End();
}

void Application::renderFrame()
{
  ImDrawData* const drawData = ImGui::GetDrawData();
  bool const minimised = drawData->DisplaySize.x <= 0.0F || drawData->DisplaySize.y <= 0.0F;

  SDL_GPUCommandBuffer* const commands = SDL_AcquireGPUCommandBuffer( mDevice );
  if ( commands == nullptr )
  {
    return;
  }

  if ( mFrameChanged )
  {
    mScreen->upload( commands, mFrame );
    mFrameChanged = false;
  }
  mVideo->upload( commands );

  SDL_GPUTexture* swapchain = nullptr;
  if ( SDL_WaitAndAcquireGPUSwapchainTexture( commands, mWindow, &swapchain, nullptr, nullptr ) &&
       swapchain != nullptr && !minimised )
  {
    // The backend has to upload its vertices before the render pass begins.
    ImGui_ImplSDLGPU3_PrepareDrawData( drawData, commands );

    SDL_GPUColorTargetInfo target{};
    target.texture = swapchain;
    target.clear_color = SDL_FColor{ .r = 0.0F, .g = 0.0F, .b = 0.0F, .a = 1.0F };
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* const pass = SDL_BeginGPURenderPass( commands, &target, 1, nullptr );
    ImGui_ImplSDLGPU3_RenderDrawData( drawData, commands, pass );
    SDL_EndGPURenderPass( pass );
  }
  SDL_SubmitGPUCommandBuffer( commands );
}

} // namespace pgm::app
