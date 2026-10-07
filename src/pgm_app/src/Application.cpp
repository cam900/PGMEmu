#include "Application.hpp"

#include "TestPattern.hpp"

#include "pgm/video/Screen.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include <algorithm>
#include <cmath>
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

std::string sdlError( std::string const& what )
{
  return what + ": " + SDL_GetError();
}

} // namespace

std::expected<std::unique_ptr<Application>, std::string> Application::create()
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
  std::unique_ptr<Application> application{ new Application{ window, device } };
  if ( !application->mScreen->valid() )
  {
    return std::unexpected( sdlError( "creating the screen texture" ) );
  }
  return application;
}

Application::Application( SDL_Window* window, SDL_GPUDevice* device )
    : mWindow{ window }, mDevice{ device }, mScreen{ std::make_unique<ScreenTexture>( device ) },
      mFrame{ makeTestPattern() }, mEmulator{ Settings{} }, mDispatcher{ mEmulator }
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

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    drawInterface();
    ImGui::Render();
    renderFrame();
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
  if ( ImGui::BeginMenu( "View" ) )
  {
    ImGui::MenuItem( STATUS_WINDOW, nullptr, &mShowStatus );
    ImGui::Separator();
    ImGui::MenuItem( "ImGui demo", nullptr, &mShowImguiDemo );
    ImGui::EndMenu();
  }
  ImGui::EndMainMenuBar();
}

void Application::drawScreenWindow()
{
  ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2{ 0.0F, 0.0F } );
  bool const open = ImGui::Begin( SCREEN_WINDOW, nullptr, ImGuiWindowFlags_NoScrollbar );
  ImGui::PopStyleVar();
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
    auto const response = mDispatcher.handle( control::Json{ { "id", 1 }, { "method", "emu.status" } } );
    if ( response.at( "ok" ) == true )
    {
      ImGui::Text( "Version: %s", response.at( "result" ).at( "version" ).get_ref<std::string const&>().c_str() );
    }
    else
    {
      ImGui::Text( "emu.status failed: %s", response.at( "error" ).dump().c_str() );
    }
    ImGui::Text( "Screen: %zux%zu", video::SCREEN_WIDTH, video::SCREEN_HEIGHT );
    ImGui::Text( "Interface: %.1f fps", static_cast<double>( ImGui::GetIO().Framerate ) );
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
