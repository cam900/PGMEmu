#include "pgm/cart/PgmImage.hpp"

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <utility>

namespace pgm::cart
{

namespace
{

// Offsets and sizes of docs/spec/pgm-format.md §2.
constexpr std::size_t HEADER_SIZE = 1024;
constexpr std::size_t INFO_SIZE = 80;
constexpr std::uint16_t VERSION = 0x0022;
constexpr std::array<std::uint8_t, 6> MAGIC{ 'I', 'G', 'S', 'P', 'G', 'M' };

constexpr std::size_t VERSION_AT = 6;
constexpr std::size_t INFO_SIZE_AT = 8;
constexpr std::size_t SHORT_NAME_AT = 12;
constexpr std::size_t SHORT_NAME_SIZE = 16;
constexpr std::size_t MANUFACTURER_AT = 28;
constexpr std::size_t ASCII_LONG_NAME_AT = 32;
constexpr std::size_t UTF8_LONG_NAME_AT = 36;
constexpr std::size_t YEAR_AT = 40;
constexpr std::size_t YEAR_SIZE = 4;
constexpr std::size_t HARDWARE_AT = 44;
constexpr std::size_t ENTRIES_AT = 52;
constexpr std::size_t ENTRIES_COUNT_AT = 56;
constexpr std::size_t REGION_OFFSET_AT = 72;
constexpr std::size_t FLAGS_AT = 76;

// Bits of the flags, §2.2.
constexpr std::uint32_t FLAG_VERTICAL = 1U << 0U;

constexpr std::size_t ENTRY_SIZE = 16;
constexpr std::size_t REGION_SIZE = 8;

using Bytes = std::span<std::uint8_t const>;

std::uint16_t readLe16( Bytes bytes, std::size_t at )
{
  return static_cast<std::uint16_t>( bytes[at] | ( bytes[at + 1] << 8U ) );
}

std::uint32_t readLe32( Bytes bytes, std::size_t at )
{
  return static_cast<std::uint32_t>( bytes[at] ) | ( static_cast<std::uint32_t>( bytes[at + 1] ) << 8U ) |
         ( static_cast<std::uint32_t>( bytes[at + 2] ) << 16U ) |
         ( static_cast<std::uint32_t>( bytes[at + 3] ) << 24U );
}

/// Characters of a fixed-size field up to its first NUL; the field need not
/// hold one when the text fills it.
std::string fixedString( Bytes bytes, std::size_t at, std::size_t size )
{
  auto const field = bytes.subspan( at, size );
  auto const end = std::ranges::find( field, std::uint8_t{ 0 } );
  return { field.begin(), end };
}

/// The NUL-terminated string a header field points at, or an empty one where
/// the field is 0. The string must lie in the header, after the info block.
std::expected<std::string, std::string> pointedString( Bytes header, std::size_t fieldAt, char const* field )
{
  std::uint32_t const offset = readLe32( header, fieldAt );
  if ( offset == 0 )
  {
    return std::string{};
  }
  if ( offset < INFO_SIZE || offset >= HEADER_SIZE )
  {
    return std::unexpected( fmt::format( "{} points at {}, outside the header's string area", field, offset ) );
  }
  auto const rest = header.subspan( offset );
  auto const end = std::ranges::find( rest, std::uint8_t{ 0 } );
  if ( end == rest.end() )
  {
    return std::unexpected( fmt::format( "{} at {} is not terminated within the header", field, offset ) );
  }
  return std::string{ rest.begin(), end };
}

std::expected<RegionInfo, std::string> parseRegionInfo( Bytes header, std::uint32_t offset )
{
  if ( offset < INFO_SIZE || offset + 4 > HEADER_SIZE )
  {
    return std::unexpected( fmt::format( "regionOffset {} is outside the header", offset ) );
  }

  RegionInfo info;
  std::uint8_t const scheme = header[offset];
  std::size_t expectedInfoSize = 0;
  switch ( scheme )
  {
  case std::to_underlying( RegionScheme::ASIC27 ):
    info.scheme = RegionScheme::ASIC27;
    expectedInfoSize = 8;
    break;
  case std::to_underlying( RegionScheme::IGS025 ):
    info.scheme = RegionScheme::IGS025;
    expectedInfoSize = 4;
    break;
  case std::to_underlying( RegionScheme::ASIC3 ):
    info.scheme = RegionScheme::ASIC3;
    expectedInfoSize = 8;
    break;
  default:
    return std::unexpected( fmt::format( "region block has unknown type {}", scheme ) );
  }

  std::size_t const count = header[offset + 1];
  std::size_t const tableOffset = readLe16( header, offset + 2 );
  if ( tableOffset != expectedInfoSize )
  {
    return std::unexpected( fmt::format( "{} region block gives its table at +{}, where +{} is expected",
                                         nameOf( info.scheme ),
                                         tableOffset,
                                         expectedInfoSize ) );
  }
  std::size_t const tableAt = offset + tableOffset;
  if ( tableAt + ( count * REGION_SIZE ) > HEADER_SIZE )
  {
    return std::unexpected( fmt::format( "region table of {} entries runs past the header", count ) );
  }

  if ( info.scheme == RegionScheme::ASIC27 )
  {
    info.patchType = readLe16( header, offset + 4 );
    info.patchOffset = readLe16( header, offset + 6 );
    if ( info.patchType != ASIC27_PATCH_BE16 && info.patchType != ASIC27_PATCH_BYTE )
    {
      return std::unexpected(
          fmt::format( "ASIC27 region block has patch type {}; 0 and 1 are defined", info.patchType ) );
    }
  }
  else if ( info.scheme == RegionScheme::ASIC3 )
  {
    info.defaultRegion = readLe32( header, offset + 4 );
  }

  info.regions.reserve( count );
  for ( std::size_t i = 0; i < count; ++i )
  {
    std::size_t const at = tableAt + ( i * REGION_SIZE );
    info.regions.push_back( Region{ .agnosticId = readLe32( header, at ), .regionId = readLe32( header, at + 4 ) } );
  }
  return info;
}

// The I25 block, docs/spec/pgm-format.md §3.1.
constexpr std::size_t IGS025_HEADER_SIZE = 3;
constexpr std::size_t IGS025_TABLE_SIZE = 1 + 4 + std::tuple_size_v<decltype( Igs025Table::data )>;

std::expected<Igs025Settings, std::string> parseIgs025Settings( Bytes block )
{
  if ( block.size() < IGS025_HEADER_SIZE )
  {
    return std::unexpected( fmt::format( "I25 block of {} bytes is shorter than its header", block.size() ) );
  }
  Igs025Settings settings{ .variant = block[0], .defaultRegion = block[1], .tables = {} };
  std::size_t const count = block[2];
  if ( block.size() != IGS025_HEADER_SIZE + ( count * IGS025_TABLE_SIZE ) )
  {
    return std::unexpected(
        fmt::format( "I25 block of {} bytes does not hold the {} tables it says it has", block.size(), count ) );
  }
  for ( std::size_t i = 0; i < count; ++i )
  {
    Bytes const entry = block.subspan( IGS025_HEADER_SIZE + ( i * IGS025_TABLE_SIZE ), IGS025_TABLE_SIZE );
    Igs025Table table{ .region = entry[0],
                       .gameId = ( static_cast<std::uint32_t>( entry[1] ) << 24U ) |
                                 ( static_cast<std::uint32_t>( entry[2] ) << 16U ) |
                                 ( static_cast<std::uint32_t>( entry[3] ) << 8U ) | entry[4],
                       .data = {} };
    std::ranges::copy( entry.subspan( 5 ), table.data.begin() );
    settings.tables.push_back( table );
  }
  return settings;
}

} // namespace

std::string_view nameOf( RomType type )
{
  switch ( type )
  {
  case RomType::NONE:
    return "NONE";
  case RomType::PRG:
    return "PRG";
  case RomType::INT:
    return "INT";
  case RomType::EXT:
    return "EXT";
  case RomType::TLE:
    return "TLE";
  case RomType::SPM:
    return "SPM";
  case RomType::SPC:
    return "SPC";
  case RomType::AUD:
    return "AUD";
  case RomType::I22:
    return "I22";
  case RomType::I25:
    return "I25";
  }
  return "?";
}

std::string_view nameOf( Hardware hardware )
{
  switch ( hardware )
  {
  case Hardware::PGM:
    return "pgm";
  case Hardware::ASIC3:
    return "asic3";
  case Hardware::IGS012_IGS025:
    return "igs012_igs025";
  case Hardware::IGS022_IGS025:
    return "igs022_igs025";
  case Hardware::ARM_TYPE1:
    return "arm_type1";
  case Hardware::ARM_TYPE2:
    return "arm_type2";
  case Hardware::ARM_TYPE3:
    return "arm_type3";
  case Hardware::IGS028_IGS025:
    return "igs028_igs025";
  case Hardware::ARM_TYPE1_CAVE:
    return "arm_type1_cave";
  }
  return "unknown";
}

std::string_view nameOf( RegionScheme scheme )
{
  switch ( scheme )
  {
  case RegionScheme::ASIC27:
    return "asic27";
  case RegionScheme::IGS025:
    return "igs025";
  case RegionScheme::ASIC3:
    return "asic3";
  }
  return "unknown";
}

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

std::string fourCc( std::uint32_t agnosticId )
{
  return { static_cast<char>( agnosticId >> 24U ),
           static_cast<char>( agnosticId >> 16U ),
           static_cast<char>( agnosticId >> 8U ),
           static_cast<char>( agnosticId ) };
}

std::optional<std::uint32_t> agnosticIdOf( std::string_view code )
{
  if ( code.size() != 4 )
  {
    return std::nullopt;
  }
  std::uint32_t id = 0;
  for ( char const c : code )
  {
    id = ( id << 8U ) | static_cast<std::uint8_t>( c );
  }
  return id;
}

std::expected<PgmImage, std::string> PgmImage::parse( std::vector<std::uint8_t> file )
{
  if ( file.size() < HEADER_SIZE )
  {
    return std::unexpected(
        fmt::format( "file of {} bytes is shorter than the {}-byte header", file.size(), HEADER_SIZE ) );
  }
  Bytes const header{ file.data(), HEADER_SIZE };

  if ( !std::ranges::equal( header.first( MAGIC.size() ), MAGIC ) )
  {
    return std::unexpected( std::string{ "not a .pgm file: the magic is not IGSPGM" } );
  }

  // The one big-endian field of the header: BCD, so that 0x0022 reads as 00.22.
  auto const version = static_cast<std::uint16_t>( ( header[VERSION_AT] << 8U ) | header[VERSION_AT + 1] );
  if ( version != VERSION )
  {
    return std::unexpected( fmt::format( "format version {:04x}; only {:04x} is read", version, VERSION ) );
  }

  std::uint32_t const infoSize = readLe32( header, INFO_SIZE_AT );
  if ( infoSize != INFO_SIZE )
  {
    return std::unexpected( fmt::format( "infoSize is {}; version {:04x} has {}", infoSize, VERSION, INFO_SIZE ) );
  }

  PgmImage image;
  image.mVersion = version;
  image.mShortName = fixedString( header, SHORT_NAME_AT, SHORT_NAME_SIZE );
  image.mYear = fixedString( header, YEAR_AT, YEAR_SIZE );
  image.mHardware = static_cast<Hardware>( readLe32( header, HARDWARE_AT ) );
  // Flags this reader does not know are left alone: they say how to show or
  // file a game, not how to run it.
  image.mOrientation =
      ( readLe32( header, FLAGS_AT ) & FLAG_VERTICAL ) != 0 ? Orientation::VERTICAL : Orientation::HORIZONTAL;

  auto manufacturer = pointedString( header, MANUFACTURER_AT, "manufacturerLongName" );
  auto asciiName = pointedString( header, ASCII_LONG_NAME_AT, "asciiLongName" );
  auto utf8Name = pointedString( header, UTF8_LONG_NAME_AT, "utf8LongName" );
  for ( auto const* text : { &manufacturer, &asciiName, &utf8Name } )
  {
    if ( !*text )
    {
      return std::unexpected( text->error() );
    }
  }
  if ( !asciiName->empty() && !utf8Name->empty() )
  {
    return std::unexpected( std::string{ "both asciiLongName and utf8LongName are given; at most one may be" } );
  }
  image.mManufacturer = std::move( *manufacturer );
  image.mLongName = asciiName->empty() ? std::move( *utf8Name ) : std::move( *asciiName );

  std::uint32_t const entriesAt = readLe32( header, ENTRIES_AT );
  std::uint32_t const entriesCount = readLe32( header, ENTRIES_COUNT_AT );
  if ( entriesCount != 0 && ( entriesAt < INFO_SIZE ||
                              std::size_t{ entriesAt } + ( std::size_t{ entriesCount } * ENTRY_SIZE ) > HEADER_SIZE ) )
  {
    return std::unexpected(
        fmt::format( "entry table of {} entries at {} does not lie within the header", entriesCount, entriesAt ) );
  }

  for ( std::uint32_t i = 0; i < entriesCount; ++i )
  {
    std::size_t const at = entriesAt + ( std::size_t{ i } * ENTRY_SIZE );
    std::uint32_t const type = readLe32( header, at );
    if ( type < std::to_underlying( RomType::PRG ) || type > std::to_underlying( RomType::I25 ) )
    {
      return std::unexpected( fmt::format( "entry {} has type {}, which the format does not define", i, type ) );
    }
    Entry const entry{ .type = static_cast<RomType>( type ),
                       .mapping = readLe32( header, at + 4 ),
                       .offset = readLe32( header, at + 8 ),
                       .size = readLe32( header, at + 12 ) };

    if ( std::ranges::any_of( image.mEntries, [&]( Entry const& seen ) { return seen.type == entry.type; } ) )
    {
      return std::unexpected( fmt::format( "entry {} repeats type {}", i, nameOf( entry.type ) ) );
    }
    if ( entry.size == 0 )
    {
      return std::unexpected( fmt::format( "{} entry is empty", nameOf( entry.type ) ) );
    }
    if ( entry.offset < HEADER_SIZE || std::size_t{ entry.offset } + entry.size > file.size() )
    {
      return std::unexpected( fmt::format( "{} entry spans {}..{}, outside the file's {} bytes of data",
                                           nameOf( entry.type ),
                                           entry.offset,
                                           std::size_t{ entry.offset } + entry.size,
                                           file.size() ) );
    }
    image.mEntries.push_back( entry );
  }

  if ( std::uint32_t const regionOffset = readLe32( header, REGION_OFFSET_AT ); regionOffset != 0 )
  {
    auto regionInfo = parseRegionInfo( header, regionOffset );
    if ( !regionInfo )
    {
      return std::unexpected( regionInfo.error() );
    }
    image.mRegionInfo = std::move( *regionInfo );
  }

  image.mFile = std::move( file );

  if ( auto const block = image.rom( RomType::I25 ) )
  {
    auto settings = parseIgs025Settings( block->data );
    if ( !settings )
    {
      return std::unexpected( settings.error() );
    }
    image.mIgs025Settings = std::move( *settings );
  }
  return image;
}

std::expected<PgmImage, std::string> PgmImage::read( std::filesystem::path const& path )
{
  std::ifstream stream{ path, std::ios::binary };
  if ( !stream )
  {
    return std::unexpected( fmt::format( "cannot open {}", path.string() ) );
  }
  std::vector<std::uint8_t> file{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
  if ( stream.bad() )
  {
    return std::unexpected( fmt::format( "cannot read {}", path.string() ) );
  }
  return parse( std::move( file ) );
}

std::uint16_t PgmImage::version() const
{
  return mVersion;
}

std::string const& PgmImage::shortName() const
{
  return mShortName;
}

std::string const& PgmImage::manufacturer() const
{
  return mManufacturer;
}

std::string const& PgmImage::longName() const
{
  return mLongName;
}

std::string const& PgmImage::year() const
{
  return mYear;
}

Hardware PgmImage::hardware() const
{
  return mHardware;
}

Orientation PgmImage::orientation() const
{
  return mOrientation;
}

std::optional<RegionInfo> const& PgmImage::regionInfo() const
{
  return mRegionInfo;
}

std::optional<Igs025Settings> const& PgmImage::igs025Settings() const
{
  return mIgs025Settings;
}

Igs025Table const* PgmImage::igs025Table( std::uint32_t region ) const
{
  if ( !mIgs025Settings )
  {
    return nullptr;
  }
  auto const table = std::ranges::find( mIgs025Settings->tables, region, &Igs025Table::region );
  return table == mIgs025Settings->tables.end() ? nullptr : &*table;
}

std::optional<std::uint32_t> PgmImage::ownRegion() const
{
  if ( !mRegionInfo )
  {
    return std::nullopt;
  }
  switch ( mRegionInfo->scheme )
  {
  case RegionScheme::ASIC3:
    return mRegionInfo->defaultRegion;
  case RegionScheme::IGS025:
    if ( !mIgs025Settings )
    {
      return std::nullopt;
    }
    return mIgs025Settings->defaultRegion;
  case RegionScheme::ASIC27:
  {
    auto const internal = rom( RomType::INT );
    std::size_t const at = mRegionInfo->patchOffset;
    std::size_t const size = mRegionInfo->patchType == ASIC27_PATCH_BE16 ? 2 : 1;
    if ( !internal || at + size > internal->data.size() )
    {
      return std::nullopt;
    }
    if ( size == 1 )
    {
      return internal->data[at];
    }
    return static_cast<std::uint32_t>( ( internal->data[at] << 8U ) | internal->data[at + 1] );
  }
  }
  return std::nullopt;
}

std::expected<std::uint32_t, std::string> PgmImage::regionValue( std::uint32_t agnosticId ) const
{
  if ( !mRegionInfo )
  {
    return std::unexpected( fmt::format( "{} has no regions to choose from", mShortName ) );
  }
  auto const region = std::ranges::find( mRegionInfo->regions, agnosticId, &Region::agnosticId );
  if ( region == mRegionInfo->regions.end() )
  {
    std::string known;
    for ( Region const& each : mRegionInfo->regions )
    {
      known += fmt::format( "{}{}", known.empty() ? "" : ", ", fourCc( each.agnosticId ) );
    }
    return std::unexpected( fmt::format( "{} has no region {}; it has {}", mShortName, fourCc( agnosticId ), known ) );
  }
  if ( mRegionInfo->scheme == RegionScheme::IGS025 && igs025Table( region->regionId ) == nullptr )
  {
    return std::unexpected( fmt::format(
        "{}'s I25 block has no table for region {} ({})", mShortName, fourCc( agnosticId ), region->regionId ) );
  }
  return region->regionId;
}

std::vector<Rom> PgmImage::roms() const
{
  std::vector<Rom> roms;
  roms.reserve( mEntries.size() );
  for ( Entry const& entry : mEntries )
  {
    roms.push_back( view( entry ) );
  }
  return roms;
}

std::optional<Rom> PgmImage::rom( RomType type ) const
{
  auto const entry = std::ranges::find( mEntries, type, &Entry::type );
  if ( entry == mEntries.end() )
  {
    return std::nullopt;
  }
  return view( *entry );
}

Rom PgmImage::view( Entry const& entry ) const
{
  return Rom{ .type = entry.type,
              .mapping = entry.mapping,
              .data = Bytes{ mFile }.subspan( entry.offset, entry.size ) };
}

} // namespace pgm::cart
