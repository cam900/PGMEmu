#include "pgm/cart/Orientation.hpp"

#include <algorithm>
#include <array>

namespace pgm::cart
{

namespace
{

/// CAVE's shooters, every set of them MAME has: their images keep MAME's
/// names.
constexpr auto VERTICAL_SETS = std::to_array<std::string_view>( {
    "ddp2",     "ddp2101",    "ddp2100",   "ddp2hk",     "ddp2101hk",  "ddp2100hk",   "ddp2k",
    "ddp2101k", "ddp2100k",   "ddp2j",     "ddp2101j",   "ddp2100j",   "ddp2t",       "ddp2101t",
    "ddp2100t", "ddp2c",      "ddp2101c",  "ddp2100c",   "ddp3",       "ddpdoj",      "ddpdoja",
    "ddpdojb",  "ddpdojp",    "ddpdojblk", "ddpdojblka", "ddpdojblkb", "ddpdojblkbl", "ket",
    "ket1",     "keta",       "ketb",      "ketbl",      "ketarr",     "ketarr151",   "ketarr15",
    "ketarr10", "ketarrs151", "ketarrs15", "ketarrf",    "ketarrb",    "espgal",      "espgalbl",
} );

} // namespace

std::string_view nameOf( Orientation orientation )
{
  switch ( orientation )
  {
  case Orientation::HORIZONTAL:
    return "horizontal";
  case Orientation::VERTICAL:
    return "vertical";
  }
  return "?";
}

Orientation orientationOf( std::string_view shortName )
{
  return std::ranges::find( VERTICAL_SETS, shortName ) != VERTICAL_SETS.end() ? Orientation::VERTICAL
                                                                              : Orientation::HORIZONTAL;
}

} // namespace pgm::cart
