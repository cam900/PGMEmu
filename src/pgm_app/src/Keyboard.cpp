#include "Keyboard.hpp"

#include <SDL3/SDL_scancode.h>

#include <initializer_list>
#include <utility>

namespace pgm::app
{

namespace
{

// IN0 holds player 1 in its low byte and player 2 in its high byte: start, up,
// down, left, right, then buttons 1-3. IN2 holds the coins in its low nibble and
// each player's button 4 from bit 8. (PGM.sv's IN0..IN3.)
constexpr std::uint16_t START = 1U << 0U;
constexpr std::uint16_t UP = 1U << 1U;
constexpr std::uint16_t DOWN = 1U << 2U;
constexpr std::uint16_t LEFT = 1U << 3U;
constexpr std::uint16_t RIGHT = 1U << 4U;
constexpr std::uint16_t BUTTON_1 = 1U << 5U;
constexpr std::uint16_t BUTTON_2 = 1U << 6U;
constexpr std::uint16_t BUTTON_3 = 1U << 7U;
constexpr std::uint16_t PLAYER_2 = 8;
constexpr std::uint16_t COIN_1 = 1U << 0U;
constexpr std::uint16_t COIN_2 = 1U << 1U;
constexpr std::uint16_t BUTTON_4_PLAYER_1 = 1U << 8U;

} // namespace

std::array<std::uint16_t, 4> inputsFromKeyboard( bool const* keys )
{
  std::array<std::uint16_t, 4> pressed{};
  auto const hold = [&]( std::size_t port, SDL_Scancode key, std::uint16_t bits )
  {
    if ( keys[key] )
    {
      pressed.at( port ) = static_cast<std::uint16_t>( pressed.at( port ) | bits );
    }
  };
  for ( auto const& [key, bits] : std::initializer_list<std::pair<SDL_Scancode, std::uint16_t>>{
            { SDL_SCANCODE_1, START },
            { SDL_SCANCODE_UP, UP },
            { SDL_SCANCODE_DOWN, DOWN },
            { SDL_SCANCODE_LEFT, LEFT },
            { SDL_SCANCODE_RIGHT, RIGHT },
            { SDL_SCANCODE_Z, BUTTON_1 },
            { SDL_SCANCODE_X, BUTTON_2 },
            { SDL_SCANCODE_C, BUTTON_3 },
            { SDL_SCANCODE_2, static_cast<std::uint16_t>( START << PLAYER_2 ) } } )
  {
    hold( 0, key, bits );
  }
  hold( 2, SDL_SCANCODE_5, COIN_1 );
  hold( 2, SDL_SCANCODE_6, COIN_2 );
  hold( 2, SDL_SCANCODE_V, BUTTON_4_PLAYER_1 );
  return pressed;
}

} // namespace pgm::app
