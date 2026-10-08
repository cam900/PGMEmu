#include "Gamepads.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>

namespace pgm::app
{

Gamepads::~Gamepads()
{
  for ( Pad const& pad : mPads )
  {
    SDL_CloseGamepad( pad.gamepad );
  }
}

void Gamepads::handle( SDL_Event const& event )
{
  if ( event.type == SDL_EVENT_GAMEPAD_ADDED )
  {
    SDL_JoystickID const id = event.gdevice.which;
    if ( indexOf( id ) >= 0 )
    {
      return;
    }
    SDL_Gamepad* const gamepad = SDL_OpenGamepad( id );
    if ( gamepad == nullptr )
    {
      spdlog::warn( "cannot open gamepad {}: {}", id, SDL_GetError() );
      return;
    }
    char const* const name = SDL_GetGamepadName( gamepad );
    mPads.push_back( Pad{ .id = id, .gamepad = gamepad, .name = name != nullptr ? name : "Gamepad" } );
    spdlog::info( "gamepad {} connected: {}", mPads.size(), mPads.back().name );
  }
  else if ( event.type == SDL_EVENT_GAMEPAD_REMOVED )
  {
    auto const pad = std::ranges::find( mPads, event.gdevice.which, &Pad::id );
    if ( pad != mPads.end() )
    {
      spdlog::info( "gamepad disconnected: {}", pad->name );
      SDL_CloseGamepad( pad->gamepad );
      mPads.erase( pad );
    }
  }
}

std::vector<Gamepads::Pad> const& Gamepads::connected() const
{
  return mPads;
}

std::vector<SDL_Gamepad*> Gamepads::handles() const
{
  std::vector<SDL_Gamepad*> handles;
  handles.reserve( mPads.size() );
  for ( Pad const& pad : mPads )
  {
    handles.push_back( pad.gamepad );
  }
  return handles;
}

int Gamepads::indexOf( SDL_JoystickID id ) const
{
  auto const pad = std::ranges::find( mPads, id, &Pad::id );
  return pad == mPads.end() ? -1 : static_cast<int>( pad - mPads.begin() );
}

} // namespace pgm::app
