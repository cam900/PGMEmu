#pragma once

#include "support/Files.hpp"

#include "pgm/Emulator.hpp"

#include <filesystem>

namespace pgm::test
{

/// A BIOS directory and a ROM directory holding `testcart.pgm`, as an emulator
/// started with `--bios` and `--rom-dir` would be given them. The BIOS is
/// filler: it loads, and its 68000 program does nothing useful.
class Fixture
{
public:
  Fixture();

  [[nodiscard]] std::filesystem::path biosDirectory() const;
  [[nodiscard]] std::filesystem::path romDirectory() const;
  [[nodiscard]] pgm::Settings settings() const;

private:
  TemporaryDirectory mScratch;
};

} // namespace pgm::test
