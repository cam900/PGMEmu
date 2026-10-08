#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>

#include <string>
#include <vector>

namespace pgm::app
{

/// The gamepads connected, opened as SDL announces them and closed as they go,
/// in the order they came: the order the input map's players take them in.
class Gamepads
{
public:
  struct Pad
  {
    SDL_JoystickID id{};
    SDL_Gamepad* gamepad{};
    std::string name;
  };

  Gamepads() = default;
  ~Gamepads();

  Gamepads( Gamepads const& ) = delete;
  Gamepads& operator=( Gamepads const& ) = delete;
  Gamepads( Gamepads&& ) = delete;
  Gamepads& operator=( Gamepads&& ) = delete;

  /// Takes a gamepad's arrival or departure; other events are left alone.
  void handle( SDL_Event const& event );

  [[nodiscard]] std::vector<Pad> const& connected() const;
  /// Each connected gamepad's handle, in order.
  [[nodiscard]] std::vector<SDL_Gamepad*> handles() const;
  /// The place in the order of the gamepad `id` is, or -1.
  [[nodiscard]] int indexOf( SDL_JoystickID id ) const;

private:
  std::vector<Pad> mPads;
};

} // namespace pgm::app
