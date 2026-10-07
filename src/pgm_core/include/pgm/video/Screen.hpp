#pragma once

#include <cstddef>

namespace pgm::video
{

/// The visible picture of the IGS023: 448 of the 640 dots of a line and 224 of
/// its 264 lines (igs023.sv). Every frame the core produces, and every texture a
/// frontend shows it in, has this size.
inline constexpr std::size_t SCREEN_WIDTH = 448;
inline constexpr std::size_t SCREEN_HEIGHT = 224;

} // namespace pgm::video
