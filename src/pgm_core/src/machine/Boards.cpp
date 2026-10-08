#include "Boards.hpp"

#include "Igs022Igs025Board.hpp"

#include <spdlog/spdlog.h>

namespace pgm::machine
{

namespace
{

// The I25 block's variants the RTL has boards for, and where their IGS025
// answers: Dragon World 3's (and Dragon World 3 EX's) at 0xDA5610, The Killing
// Blade's at 0xD40000.
constexpr std::uint8_t VARIANT_DRAGON_WORLD_3 = 1;
constexpr std::uint8_t VARIANT_KILLING_BLADE = 2;
constexpr std::uint32_t DRAGON_WORLD_3_IGS025 = 0xda5610;
constexpr std::uint32_t KILLING_BLADE_IGS025 = 0xd40000;

std::unique_ptr<Protection> makeIgs022Igs025( cart::PgmImage const& cartridge, std::uint32_t region )
{
  auto const& settings = cartridge.igs025Settings();
  auto const igs022Rom = cartridge.rom( cart::RomType::I22 );
  cart::Igs025Table const* const table = cartridge.igs025Table( region );
  if ( !settings || !igs022Rom || table == nullptr )
  {
    spdlog::warn(
        "{} has no I22 ROM, or no I25 table for region {}; it runs unprotected", cartridge.shortName(), region );
    return nullptr;
  }
  switch ( settings->variant )
  {
  case VARIANT_DRAGON_WORLD_3:
    return std::make_unique<Igs022Igs025Board>( igs022Rom->data, *table, DRAGON_WORLD_3_IGS025 );
  case VARIANT_KILLING_BLADE:
    return std::make_unique<Igs022Igs025Board>( igs022Rom->data, *table, KILLING_BLADE_IGS025 );
  default:
    spdlog::warn( "{}'s IGS025 is variant {}, which the RTL has no board for; it runs unprotected",
                  cartridge.shortName(),
                  settings->variant );
    return nullptr;
  }
}

} // namespace

std::unique_ptr<Protection> makeProtection( cart::PgmImage const& cartridge, std::uint32_t region )
{
  if ( cartridge.hardware() == cart::Hardware::IGS022_IGS025 )
  {
    return makeIgs022Igs025( cartridge, region );
  }
  return nullptr;
}

} // namespace pgm::machine
