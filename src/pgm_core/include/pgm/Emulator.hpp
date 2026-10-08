#pragma once

#include "pgm/cart/Bios.hpp"
#include "pgm/cart/PgmImage.hpp"
#include "pgm/machine/Machine.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pgm
{

/// Where the emulator finds what it is asked to load. Supplied by whoever
/// starts it, so that the core never decides where to look on its own.
struct Settings
{
  /// Directories and zip archives the BIOS files are taken from, the first
  /// that has a file winning (io::RomSources).
  std::vector<std::filesystem::path> biosSources;
  /// The directory `<name>.pgm` is looked for in when a game is loaded by name.
  std::filesystem::path romDirectory;
  /// Where `state.save` and `state.load` keep save states named without a
  /// directory, and `state.list` looks; the working directory when empty.
  std::filesystem::path stateDirectory;
};

/// Why a game could not be loaded. `kind` is what the control protocol reports
/// as an error code; `message` says which file and why.
struct LoadFailure
{
  enum class Kind : std::uint8_t
  {
    UNKNOWN_GAME,
    LOAD_FAILED
  };

  Kind kind{};
  std::string message;
};

/// A named block of the emulator's memory, as `memory.read` addresses it.
struct MemoryRegion
{
  std::string_view name;
  std::span<std::uint8_t const> bytes;
};

/// The emulated PGM as a whole: what is loaded into it, and, as later
/// milestones add them, the machine that runs it.
class Emulator
{
public:
  /// The name that loads the motherboard alone, with no cartridge. It is the
  /// name of the BIOS set in MAME and in the RTL simulator.
  static constexpr std::string_view BIOS_ONLY = "pgm";

  explicit Emulator( Settings settings );

  /// Loads `<romDirectory>/<name>.pgm` with the BIOS, or the BIOS alone when
  /// `name` is BIOS_ONLY. A name is a set name, not a path: it holds only
  /// letters, digits and underscores. On failure what was loaded stays loaded.
  std::expected<void, LoadFailure> loadGameByName( std::string_view name );

  /// Loads the `.pgm` at `path` with the BIOS. On failure what was loaded
  /// stays loaded.
  std::expected<void, LoadFailure> loadGameFromFile( std::filesystem::path const& path );

  /// The short name of the loaded cartridge, BIOS_ONLY for the BIOS alone, or
  /// nothing before a game is loaded.
  [[nodiscard]] std::optional<std::string> gameName() const;

  [[nodiscard]] cart::Bios const* bios() const;
  [[nodiscard]] cart::PgmImage const* cartridge() const;

  /// The board the loaded game runs on, powered up when it was loaded; null
  /// before a game is loaded.
  [[nodiscard]] machine::Machine* machine();
  [[nodiscard]] machine::Machine const* machine() const;

  [[nodiscard]] Settings const& settings() const;

  /// Every region that holds something now, in a stable order.
  [[nodiscard]] std::vector<MemoryRegion> memoryRegions() const;

private:
  [[nodiscard]] std::expected<cart::Bios, LoadFailure> loadBios() const;

  /// Makes `bios` and `cartridge` the loaded game and powers a new board up
  /// with them.
  void install( cart::Bios bios, std::optional<cart::PgmImage> cartridge );

  Settings mSettings;
  std::optional<cart::Bios> mBios;
  std::optional<cart::PgmImage> mCartridge;
  // Declared last, so that it goes before the ROMs it reads from.
  std::unique_ptr<machine::Machine> mMachine;
};

} // namespace pgm
