#include "Boards.hpp"

#include "Igs022Igs025Board.hpp"
#include "Igs027a.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <string_view>

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

using Type = Igs027aBoard::Type;

// The IGS027A boards of address_translator.sv and PGM.sv, by the set the
// RTL's loader names. Type 1 shares 64 bytes at 0x4F0000, or, on the CAVE
// games, nothing; type 2 64 KB at 0xD00000 with its latch at 0xD10000; type 3
// 64 KB at 0x500000 with its latch at 0x5C0300 and FIQ at 0x5C0000.
constexpr Igs027aBoard KOVSH{
  .type = Type::TYPE1, .latch = 0x500000, .latchBytes = 4, .share = 0x4f0000, .shareBytes = 0x40, .fiq = 0
};
constexpr Igs027aBoard KET{
  .type = Type::TYPE1, .latch = 0x400000, .latchBytes = 4, .share = 0, .shareBytes = 0, .fiq = 0
};
constexpr Igs027aBoard DDP3{
  .type = Type::TYPE1, .latch = 0x500000, .latchBytes = 4, .share = 0, .shareBytes = 0, .fiq = 0
};
constexpr Igs027aBoard KOV2{
  .type = Type::TYPE2, .latch = 0xd10000, .latchBytes = 2, .share = 0xd00000, .shareBytes = 0x10000, .fiq = 0
};
constexpr Igs027aBoard THEGLAD{
  .type = Type::TYPE3, .latch = 0x5c0300, .latchBytes = 2, .share = 0x500000, .shareBytes = 0x10000, .fiq = 0x5c0000
};

constexpr Igs027aBoard clocked( Igs027aBoard board, std::uint32_t n, std::uint32_t m )
{
  board.clockN = n;
  board.clockM = m;
  return board;
}

struct ArmGame
{
  std::string_view name;
  Igs027aBoard board;
};

constexpr std::array<ArmGame, 16> ARM_GAMES{ {
    { .name = "kovsh", .board = KOVSH },
    { .name = "photoy2k", .board = KOVSH },
    { .name = "ket", .board = KET },
    { .name = "espgal", .board = KET },
    { .name = "ddp3", .board = DDP3 },
    { .name = "kov2", .board = KOV2 },
    { .name = "kov2p", .board = KOV2 },
    { .name = "ddp2", .board = KOV2 },
    { .name = "martmast", .board = clocked( KOV2, 11, 25 ) },
    { .name = "dw2001", .board = clocked( KOV2, 11, 25 ) },
    { .name = "dwpc", .board = clocked( KOV2, 11, 25 ) },
    { .name = "dmnfrnt", .board = clocked( THEGLAD, 11, 25 ) },
    { .name = "theglad", .board = clocked( THEGLAD, 11, 25 ) },
    { .name = "svg", .board = clocked( THEGLAD, 33, 50 ) },
    { .name = "killbldp", .board = clocked( THEGLAD, 506, 747 ) },
    { .name = "happy6", .board = clocked( THEGLAD, 12, 25 ) },
} };

/// The board `cartridge` runs on: its set's, or, for a set the RTL does not
/// name, its hardware class's first.
std::optional<Igs027aBoard> armBoard( cart::PgmImage const& cartridge )
{
  auto const game = std::ranges::find( ARM_GAMES, cartridge.shortName(), &ArmGame::name );
  if ( game != ARM_GAMES.end() )
  {
    return game->board;
  }
  std::optional<Igs027aBoard> board;
  switch ( cartridge.hardware() )
  {
  case cart::Hardware::ARM_TYPE1:
    board = KOVSH;
    break;
  case cart::Hardware::ARM_TYPE1_CAVE:
    board = KET;
    break;
  case cart::Hardware::ARM_TYPE2:
    board = KOV2;
    break;
  case cart::Hardware::ARM_TYPE3:
    board = clocked( THEGLAD, 11, 25 );
    break;
  default:
    return std::nullopt;
  }
  spdlog::warn( "{} is not a set the RTL names; it runs on the {} board its hardware class has",
                cartridge.shortName(),
                cart::nameOf( cartridge.hardware() ) );
  return board;
}

/// The internal ROM with region value `region` where an ASIC27's region
/// block says the ROM holds it.
std::vector<std::uint8_t> patchedInternalRom( cart::PgmImage const& cartridge, std::uint32_t region )
{
  auto const rom = cartridge.rom( cart::RomType::INT );
  std::vector<std::uint8_t> bytes =
      rom ? std::vector<std::uint8_t>( rom->data.begin(), rom->data.end() ) : std::vector<std::uint8_t>{};
  auto const& info = cartridge.regionInfo();
  if ( !info || info->scheme != cart::RegionScheme::ASIC27 )
  {
    return bytes;
  }
  std::size_t const at = info->patchOffset;
  if ( info->patchType == cart::ASIC27_PATCH_BE16 && at + 2 <= bytes.size() )
  {
    bytes[at] = static_cast<std::uint8_t>( region >> 8U );
    bytes[at + 1] = static_cast<std::uint8_t>( region );
  }
  else if ( info->patchType == cart::ASIC27_PATCH_BYTE && at < bytes.size() )
  {
    bytes[at] = static_cast<std::uint8_t>( region );
  }
  return bytes;
}

} // namespace

std::unique_ptr<Protection> makeProtection( cart::PgmImage const& cartridge, std::uint32_t region )
{
  if ( cartridge.hardware() == cart::Hardware::IGS022_IGS025 )
  {
    return makeIgs022Igs025( cartridge, region );
  }
  if ( auto const board = armBoard( cartridge ) )
  {
    auto const external = cartridge.rom( cart::RomType::EXT );
    return std::make_unique<Igs027a>(
        *board, patchedInternalRom( cartridge, region ), external ? external->data : std::span<std::uint8_t const>{} );
  }
  return nullptr;
}

} // namespace pgm::machine
