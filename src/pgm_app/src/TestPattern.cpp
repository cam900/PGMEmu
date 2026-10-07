#include "TestPattern.hpp"

#include "pgm/video/Screen.hpp"

#include <array>
#include <cstddef>

namespace pgm::app
{

namespace
{

struct Rgb
{
  std::uint8_t red;
  std::uint8_t green;
  std::uint8_t blue;
};

// The eight colours of the 75% SMPTE bars, in their usual order.
constexpr std::array<Rgb, 8> BARS{ { { .red = 191, .green = 191, .blue = 191 },
                                     { .red = 191, .green = 191, .blue = 0 },
                                     { .red = 0, .green = 191, .blue = 191 },
                                     { .red = 0, .green = 191, .blue = 0 },
                                     { .red = 191, .green = 0, .blue = 191 },
                                     { .red = 191, .green = 0, .blue = 0 },
                                     { .red = 0, .green = 0, .blue = 191 },
                                     { .red = 0, .green = 0, .blue = 0 } } };

constexpr std::size_t GRID_CELL = 16;

Rgb colourAt( std::size_t x, std::size_t y )
{
  using video::SCREEN_HEIGHT;
  using video::SCREEN_WIDTH;

  if ( x == 0 || y == 0 || x == SCREEN_WIDTH - 1 || y == SCREEN_HEIGHT - 1 )
  {
    return Rgb{ .red = 255, .green = 255, .blue = 255 };
  }
  if ( y < SCREEN_HEIGHT * 2 / 3 )
  {
    return BARS[x * BARS.size() / SCREEN_WIDTH];
  }
  bool const light = ( ( x / GRID_CELL ) + ( y / GRID_CELL ) ) % 2 == 0;
  return light ? Rgb{ .red = 96, .green = 96, .blue = 96 } : Rgb{ .red = 32, .green = 32, .blue = 32 };
}

} // namespace

std::vector<std::uint8_t> makeTestPattern()
{
  using video::SCREEN_HEIGHT;
  using video::SCREEN_WIDTH;

  std::vector<std::uint8_t> pixels;
  pixels.reserve( SCREEN_WIDTH * SCREEN_HEIGHT * 4 );
  for ( std::size_t y = 0; y < SCREEN_HEIGHT; ++y )
  {
    for ( std::size_t x = 0; x < SCREEN_WIDTH; ++x )
    {
      auto const [red, green, blue] = colourAt( x, y );
      pixels.insert( pixels.end(), { red, green, blue, 255 } );
    }
  }
  return pixels;
}

} // namespace pgm::app
