#pragma once

#include "pgm/io/RomSources.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace pgm::cart
{

/// One of the three ROMs on the PGM motherboard.
struct BiosRom
{
  /// The file it was read from, as MAME's `pgm` set names it.
  std::string fileName;
  /// The place in the RomSources it came from.
  std::filesystem::path origin;
  /// As the file holds it. For the program that is the 68k's 16-bit words
  /// with the low byte first, the order of every PGM program ROM file.
  std::vector<std::uint8_t> data;
  std::uint32_t crc{};
  /// Whether `crc` is that of the dump in MAME's set. A replacement such as
  /// PGMTest's program is loaded all the same, and only reported as unknown.
  bool known{};
};

/// The motherboard's ROMs, which a `.pgm` cartridge image does not carry:
/// the 68k program (`pgm_p02s.u20`), the tiles of the text layer
/// (`pgm_t01s.rom`) and the ICS2115 samples (`pgm_m01s.rom`).
class Bios
{
public:
  /// Takes each of the three from the first of `sources` that has it.
  /// Answers which one is missing, or of the wrong size.
  static std::expected<Bios, std::string> load( io::RomSources const& sources );

  [[nodiscard]] BiosRom const& program() const;
  [[nodiscard]] BiosRom const& tiles() const;
  [[nodiscard]] BiosRom const& music() const;

private:
  Bios() = default;

  BiosRom mProgram;
  BiosRom mTiles;
  BiosRom mMusic;
};

} // namespace pgm::cart
