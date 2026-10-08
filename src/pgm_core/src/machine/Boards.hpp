#pragma once

// Which protection a cartridge brings to the board. The RTL is told the game
// by its loader and decodes per game (address_translator.sv, PGM.sv at MiSTer
// core commit e898860); here the cartridge's own data decides where it can,
// and its set name where only the game tells.

#include "Protection.hpp"

#include "pgm/cart/PgmImage.hpp"

#include <cstdint>
#include <memory>

namespace pgm::machine
{

/// The protection `cartridge` brings, its game made region value `region`;
/// null for a cartridge without one, or with one this emulator does not have.
/// The cartridge must outlive it.
[[nodiscard]] std::unique_ptr<Protection> makeProtection( cart::PgmImage const& cartridge, std::uint32_t region );

} // namespace pgm::machine
