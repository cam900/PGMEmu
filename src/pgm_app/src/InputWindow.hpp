#pragma once

#include "Gamepads.hpp"
#include "InputMap.hpp"

#include <SDL3/SDL_events.h>

#include <cstddef>
#include <functional>
#include <optional>

namespace pgm::app
{

/// The input map's editor: a tab a player, each control's bindings with a way
/// to add one by pressing it and to clear them, and which gamepad is whose.
class InputWindow
{
public:
  /// `changed` is called whenever the map is changed.
  InputWindow( InputMap& map, Gamepads const& gamepads, std::function<void()> changed );

  /// Lays the window out while `open`, which its close button clears.
  void draw( bool& open );

  /// While a control waits for its new binding: takes `event` as it, when it
  /// is a key, a gamepad's button or a stick pushed most of the way, and
  /// answers whether it took the event. Escape gives up waiting.
  bool capture( SDL_Event const& event );

  /// Whether a control waits for its new binding; the game is then given
  /// nothing.
  [[nodiscard]] bool capturing() const;

private:
  void drawPlayer( std::size_t player );

  struct Waiting
  {
    std::size_t player;
    Control control;
  };

  InputMap& mMap;
  Gamepads const& mGamepads;
  std::function<void()> mChanged;
  std::optional<Waiting> mWaiting;
};

} // namespace pgm::app
