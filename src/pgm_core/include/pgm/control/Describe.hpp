#pragma once

#include "pgm/cart/PgmImage.hpp"
#include "pgm/control/Dispatcher.hpp"

namespace pgm::control
{

/// What `emu.cartridge_info` answers about `image`, docs/spec/control-protocol.md.
/// `pgmemu-cli --info` prints the same, so that a person and an agent read one
/// description of a file.
Json describe( cart::PgmImage const& image );

} // namespace pgm::control
