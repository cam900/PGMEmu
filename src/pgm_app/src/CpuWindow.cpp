#include "CpuWindow.hpp"

#include <imgui.h>
#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>

namespace pgm::app
{

CpuWindow::CpuWindow( Request request ) : mRequest{ std::move( request ) }
{
}

void CpuWindow::draw( bool& open )
{
  if ( !open )
  {
    return;
  }
  if ( ImGui::Begin( "CPU state", &open ) )
  {
    if ( ImGui::BeginTabBar( "CPUs" ) )
    {
      if ( ImGui::BeginTabItem( "68000" ) )
      {
        ImGui::Text( "68000" );
        ImGui::Separator();
        ImGui::EndTabItem();
      }
      if ( ImGui::BeginTabItem( "Z80" ) )
      {
        ImGui::Text( "Z80" );
        ImGui::Separator();
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
  }
  ImGui::End();
}

} // namespace pgm::app
