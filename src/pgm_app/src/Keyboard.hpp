#pragma once

#include <array>
#include <cstdint>

namespace pgm::app
{

/// The board's input words IN0..IN3 (PGM.sv), with a set bit for each control
/// held on the keyboard: player 1 on the arrows and Z, X, C, V, with 1 to
/// start and 5 for a coin; player 2's start and coin on 2 and 6.
/// `keys` is SDL's keyboard state, indexed by scancode.
std::array<std::uint16_t, 4> inputsFromKeyboard( bool const* keys );

} // namespace pgm::app
