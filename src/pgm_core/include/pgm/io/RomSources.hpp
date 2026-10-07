#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pgm::io
{

/// The CRC-32 a ROM is known by in MAME's sets and PGMBuilder's database.
std::uint32_t crc32( std::span<std::uint8_t const> bytes );

/// An ordered list of places ROM files are looked for: directories and zip
/// archives. A file is taken from the first place that has it, so a place put
/// earlier overrides one put later -- which is how a test BIOS in a build
/// directory stands in for one file of `pgm.zip` and leaves the others to it.
class RomSources
{
public:
  /// Answers why `places` cannot be used: a path that does not exist, a file
  /// that is not a zip, or a zip that cannot be opened.
  static std::expected<RomSources, std::string> open( std::span<std::filesystem::path const> places );

  /// The content of `name`, from the first place that has a file of that name.
  [[nodiscard]] std::optional<std::vector<std::uint8_t>> find( std::string_view name ) const;

  /// Where find() took `name` from, for telling a person.
  [[nodiscard]] std::optional<std::filesystem::path> origin( std::string_view name ) const;

private:
  struct Place
  {
    std::filesystem::path path;
    bool zip{};
    /// Names of a zip's entries, read once when it is opened.
    std::vector<std::string> entries;
  };

  [[nodiscard]] Place const* placeOf( std::string_view name ) const;

  std::vector<Place> mPlaces;
};

} // namespace pgm::io
