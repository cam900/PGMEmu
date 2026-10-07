#pragma once

#include <cstdint>
#include <vector>

namespace pgm::app
{

/// A picture to show where a game's frame will be, until the core produces one:
/// colour bars over a grid, framed by a one-pixel border, so that scaling,
/// cropping and filtering of the screen are each visible at a glance. Pixels
/// are RGBA, one byte each, row by row.
std::vector<std::uint8_t> makeTestPattern();

} // namespace pgm::app
