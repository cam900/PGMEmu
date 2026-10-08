#include "InputWindow.hpp"

#include <imgui.h>
#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>

namespace pgm::app
{

namespace
{

/// How far a stick must be pushed to be taken as a binding: most of the way,
/// so that a resting stick's drift is not.
constexpr int CAPTURE_THRESHOLD = 24000;

} // namespace

InputWindow::InputWindow( InputMap& map, Gamepads const& gamepads, std::function<void()> changed )
    : mMap{ map }, mGamepads{ gamepads }, mChanged{ std::move( changed ) }
{
}

void InputWindow::draw( bool& open )
{
  if ( !open )
  {
    mWaiting.reset();
    return;
  }
  if ( ImGui::Begin( "Input", &open ) )
  {
    if ( ImGui::BeginTabBar( "players" ) )
    {
      for ( std::size_t player = 0; player < PLAYERS; ++player )
      {
        if ( ImGui::BeginTabItem( fmt::format( "Player {}", player + 1 ).c_str() ) )
        {
          drawPlayer( player );
          ImGui::EndTabItem();
        }
      }
      if ( ImGui::BeginTabItem( "Hotkeys" ) )
      {
        drawHotkeys();
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
    ImGui::Separator();
    if ( ImGui::Button( "Restore the defaults" ) )
    {
      mMap = InputMap::defaults();
      mWaiting.reset();
      mChanged();
    }
  }
  ImGui::End();
}

void InputWindow::drawPlayer( std::size_t player )
{
  InputMap::Player& entry = mMap.players().at( player );
  auto const& pads = mGamepads.connected();

  // The gamepad is chosen by its place in the order gamepads came in.
  auto const padName = [&]( int index )
  {
    if ( index < 0 )
    {
      return std::string{ "None" };
    }
    auto const at = static_cast<std::size_t>( index );
    return fmt::format(
        "Gamepad {}{}", index + 1, at < pads.size() ? fmt::format( ": {}", pads.at( at ).name ) : " (not connected)" );
  };
  if ( ImGui::BeginCombo( "Gamepad", padName( entry.gamepad ).c_str() ) )
  {
    int const choices = static_cast<int>( std::max<std::size_t>( pads.size(), PLAYERS ) );
    for ( int index = -1; index < choices; ++index )
    {
      if ( ImGui::Selectable( padName( index ).c_str(), entry.gamepad == index ) && entry.gamepad != index )
      {
        entry.gamepad = index;
        mChanged();
      }
    }
    ImGui::EndCombo();
  }

  if ( !ImGui::BeginTable( "controls", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp ) )
  {
    return;
  }
  ImGui::TableSetupColumn( "Control", ImGuiTableColumnFlags_WidthFixed );
  ImGui::TableSetupColumn( "Bound to" );
  ImGui::TableSetupColumn( "", ImGuiTableColumnFlags_WidthFixed );
  for ( std::size_t c = 0; c < CONTROLS; ++c )
  {
    drawBindings( labelOf( static_cast<Control>( c ) ), entry.bindings.at( c ) );
  }
  ImGui::EndTable();
}

void InputWindow::drawHotkeys()
{
  ImGui::TextWrapped( "Keys for the emulator rather than the game. A gamepad's binding works on any gamepad." );
  if ( !ImGui::BeginTable( "hotkeys", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp ) )
  {
    return;
  }
  ImGui::TableSetupColumn( "Hotkey", ImGuiTableColumnFlags_WidthFixed );
  ImGui::TableSetupColumn( "Bound to" );
  ImGui::TableSetupColumn( "", ImGuiTableColumnFlags_WidthFixed );
  for ( std::size_t h = 0; h < HOTKEYS; ++h )
  {
    drawBindings( labelOf( static_cast<Hotkey>( h ) ), mMap.hotkeys().at( h ) );
  }
  ImGui::EndTable();
}

void InputWindow::drawBindings( std::string_view label, std::vector<Binding>& bindings )
{
  ImGui::PushID( &bindings );
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  ImGui::TextUnformatted( std::string{ label }.c_str() );
  ImGui::TableNextColumn();
  if ( mWaiting && mWaiting->bindings == &bindings )
  {
    ImGui::TextDisabled( "Press a key or a gamepad button; Escape gives up" );
  }
  else
  {
    std::string text;
    for ( Binding const& binding : bindings )
    {
      text += fmt::format( "{}{}", text.empty() ? "" : ", ", describe( binding ) );
    }
    ImGui::TextUnformatted( text.empty() ? "-" : text.c_str() );
  }
  ImGui::TableNextColumn();
  if ( ImGui::SmallButton( "Add" ) )
  {
    mWaiting = Waiting{ .bindings = &bindings };
  }
  ImGui::SameLine();
  if ( ImGui::SmallButton( "Clear" ) && !bindings.empty() )
  {
    bindings.clear();
    mChanged();
  }
  ImGui::PopID();
}

bool InputWindow::capture( SDL_Event const& event )
{
  if ( !mWaiting )
  {
    return false;
  }
  std::optional<Binding> binding;
  if ( event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat )
  {
    if ( event.key.scancode == SDL_SCANCODE_ESCAPE )
    {
      mWaiting.reset();
      return true;
    }
    binding = Binding{ .kind = Binding::Kind::KEY, .code = event.key.scancode, .direction = 0 };
  }
  else if ( event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN )
  {
    binding = Binding{ .kind = Binding::Kind::BUTTON, .code = event.gbutton.button, .direction = 0 };
  }
  else if ( event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && std::abs( event.gaxis.value ) > CAPTURE_THRESHOLD &&
            event.gaxis.axis != SDL_GAMEPAD_AXIS_LEFT_TRIGGER && event.gaxis.axis != SDL_GAMEPAD_AXIS_RIGHT_TRIGGER )
  {
    binding =
        Binding{ .kind = Binding::Kind::AXIS, .code = event.gaxis.axis, .direction = event.gaxis.value < 0 ? -1 : 1 };
  }
  else if ( event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && event.gaxis.value > CAPTURE_THRESHOLD )
  {
    // A trigger, which rests at 0 and is pulled towards its positive end.
    binding = Binding{ .kind = Binding::Kind::AXIS, .code = event.gaxis.axis, .direction = 1 };
  }
  if ( !binding )
  {
    // Key releases and the like are swallowed while waiting.
    return event.type == SDL_EVENT_KEY_UP || event.type == SDL_EVENT_KEY_DOWN;
  }
  auto& bindings = *mWaiting->bindings;
  if ( std::ranges::find( bindings, *binding ) == bindings.end() )
  {
    bindings.push_back( *binding );
    mChanged();
  }
  mWaiting.reset();
  return true;
}

bool InputWindow::capturing() const
{
  return mWaiting.has_value();
}

} // namespace pgm::app
