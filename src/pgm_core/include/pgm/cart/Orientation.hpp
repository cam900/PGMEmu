#pragma once

#include <cstdint>
#include <string_view>

namespace pgm::cart
{

/// Which way up a game's monitor stood in its cabinet
/// (docs/decisions/0016-vertical-games-by-set-name.md). The board draws 448 by
/// 224 either way; a vertical game draws its picture lying on its side.
enum class Orientation : std::uint8_t
{
  HORIZONTAL,
  /// The monitor turned a quarter anticlockwise: the picture's top is at the
  /// player's left, and it is seen upright turned a quarter anticlockwise.
  VERTICAL
};

/// "horizontal" or "vertical".
std::string_view nameOf( Orientation orientation );

/// The orientation of the set named `shortName`, as a `.pgm` header names it:
/// vertical for the sets MAME draws ROT270 and the MiSTer core's MRAs call
/// vertical, horizontal for every other.
Orientation orientationOf( std::string_view shortName );

} // namespace pgm::cart
