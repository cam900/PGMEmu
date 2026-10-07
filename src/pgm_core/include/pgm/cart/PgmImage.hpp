#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pgm::cart
{

/// What a ROM entry of a `.pgm` file holds. The values are the file's own,
/// docs/spec/pgm-format.md §3. NONE never appears in a file the reader
/// accepts; it is what a Rom holds before it is given a type.
enum class RomType : std::uint8_t
{
  NONE = 0,
  PRG = 1,
  INT = 2,
  EXT = 3,
  TLE = 4,
  SPM = 5,
  SPC = 6,
  AUD = 7,
  I22 = 8,
  I25 = 9
};

/// The three-letter name the format gives `type`, or "?" for a value the
/// format does not define.
std::string_view nameOf( RomType type );

/// The board a cartridge needs, as the header's `hardware` field says it.
/// docs/spec/pgm-format.md §2.1 lists the values; a value outside them is kept
/// as read, since deciding whether a board can be emulated is not the reader's
/// business -- which is also why the type is as wide as the field.
enum class Hardware : std::uint32_t // NOLINT(performance-enum-size): holds any value the file has
{
  PGM = 0,
  ASIC3 = 1,
  IGS012_IGS025 = 2,
  IGS022_IGS025 = 3,
  ARM_TYPE1 = 4,
  ARM_TYPE2 = 5,
  ARM_TYPE3 = 6,
  IGS028_IGS025 = 7,
  ARM_TYPE1_CAVE = 8
};

/// A name for `hardware`, or "unknown" for a value the format does not define.
std::string_view nameOf( Hardware hardware );

/// How a game is told which region it is: by the chip that answers the
/// question, docs/spec/pgm-format.md §4.
enum class RegionScheme : std::uint8_t
{
  ASIC27 = 0,
  IGS025 = 1,
  ASIC3 = 2
};

std::string_view nameOf( RegionScheme scheme );

struct Region
{
  /// One of the four-character codes of docs/spec/pgm-format.md §4.3, such as
  /// `WRLD`, held as the file holds it.
  std::uint32_t agnosticId{};
  /// The value the game's protection hands it for this region.
  std::uint32_t regionId{};
};

/// The four characters of `agnosticId`, most significant first: `WRLD`.
std::string fourCc( std::uint32_t agnosticId );

struct RegionInfo
{
  RegionScheme scheme{};
  std::vector<Region> regions;
  /// ASIC27 only: how and where the region is patched into the program.
  std::uint16_t patchType{};
  std::uint16_t patchOffset{};
  /// ASIC3 only: the region the game runs as unless told otherwise.
  std::uint32_t defaultRegion{};
};

/// One ROM of the cartridge: the bytes, and the address they are mapped from.
struct Rom
{
  RomType type{};
  std::uint32_t mapping{};
  std::span<std::uint8_t const> data;
};

/// A `.pgm` cartridge image, read and checked against docs/spec/pgm-format.md.
/// It owns the file's bytes; every Rom it hands out is a view into them.
class PgmImage
{
public:
  /// Reads `file`, the whole content of a `.pgm`. Answers why it cannot, in a
  /// sentence naming the field at fault.
  static std::expected<PgmImage, std::string> parse( std::vector<std::uint8_t> file );

  /// Reads the file at `path`, as parse() does.
  static std::expected<PgmImage, std::string> read( std::filesystem::path const& path );

  [[nodiscard]] std::uint16_t version() const;
  [[nodiscard]] std::string const& shortName() const;
  [[nodiscard]] std::string const& manufacturer() const;
  [[nodiscard]] std::string const& longName() const;
  [[nodiscard]] std::string const& year() const;
  [[nodiscard]] Hardware hardware() const;
  [[nodiscard]] std::optional<RegionInfo> const& regionInfo() const;

  /// Every ROM, in the order the file lists them.
  [[nodiscard]] std::vector<Rom> roms() const;

  /// The ROM of `type`, if the cartridge has one.
  [[nodiscard]] std::optional<Rom> rom( RomType type ) const;

private:
  struct Entry
  {
    RomType type{};
    std::uint32_t mapping{};
    std::uint32_t offset{};
    std::uint32_t size{};
  };

  PgmImage() = default;

  [[nodiscard]] Rom view( Entry const& entry ) const;

  std::vector<std::uint8_t> mFile;
  std::uint16_t mVersion{};
  std::string mShortName;
  std::string mManufacturer;
  std::string mLongName;
  std::string mYear;
  Hardware mHardware{};
  std::vector<Entry> mEntries;
  std::optional<RegionInfo> mRegionInfo;
};

} // namespace pgm::cart
