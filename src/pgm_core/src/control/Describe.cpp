#include "pgm/control/Describe.hpp"

#include "pgm/cart/Orientation.hpp"
#include "pgm/io/RomSources.hpp"

#include <spdlog/fmt/fmt.h>

#include <utility>

namespace pgm::control
{

Json describe( cart::PgmImage const& image )
{
  Json roms = Json::array();
  for ( cart::Rom const& rom : image.roms() )
  {
    roms.push_back( Json{ { "type", cart::nameOf( rom.type ) },
                          { "mapping", rom.mapping },
                          { "size", rom.data.size() },
                          { "crc32", fmt::format( "{:08x}", io::crc32( rom.data ) ) } } );
  }

  Json regionInfo = nullptr;
  if ( auto const& info = image.regionInfo() )
  {
    Json regions = Json::array();
    for ( cart::Region const& region : info->regions )
    {
      regions.push_back( Json{ { "id", cart::fourCc( region.agnosticId ) }, { "value", region.regionId } } );
    }
    regionInfo = Json{ { "scheme", cart::nameOf( info->scheme ) } };
    if ( info->scheme == cart::RegionScheme::ASIC27 )
    {
      regionInfo["patch_type"] = info->patchType;
      regionInfo["patch_offset"] = info->patchOffset;
    }
    if ( info->scheme == cart::RegionScheme::ASIC3 )
    {
      regionInfo["default_region"] = info->defaultRegion;
    }
    regionInfo["regions"] = std::move( regions );
  }

  return Json{ { "short_name", image.shortName() },
               { "long_name", image.longName() },
               { "manufacturer", image.manufacturer() },
               { "year", image.year() },
               { "format_version", fmt::format( "{:04x}", image.version() ) },
               { "hardware", cart::nameOf( image.hardware() ) },
               { "orientation", cart::nameOf( cart::orientationOf( image.shortName() ) ) },
               { "roms", std::move( roms ) },
               { "region_info", std::move( regionInfo ) } };
}

} // namespace pgm::control
