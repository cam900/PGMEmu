#include "support/Fixture.hpp"

#include "support/PgmFile.hpp"

#include <cstdint>
#include <vector>

namespace pgm::test
{

Fixture::Fixture()
{
  std::filesystem::create_directory( biosDirectory() );
  std::filesystem::create_directory( romDirectory() );
  writeFile( biosDirectory() / "pgm_p02s.u20", std::vector<std::uint8_t>( 0x20000, 0xb0 ) );
  writeFile( biosDirectory() / "pgm_t01s.rom", std::vector<std::uint8_t>( 0x200000, 0xb1 ) );
  writeFile( biosDirectory() / "pgm_m01s.rom", std::vector<std::uint8_t>( 0x200000, 0xb2 ) );

  PgmFile cart;
  cart.roms = { PgmRom{ .type = 1, .mapping = 0x100000, .data = { 0x4e, 0x71, 0x4e, 0x75 } } };
  writeFile( romDirectory() / "testcart.pgm", write( cart ) );
}

std::filesystem::path Fixture::biosDirectory() const
{
  return mScratch.path() / "bios";
}

std::filesystem::path Fixture::romDirectory() const
{
  return mScratch.path() / "roms";
}

pgm::Settings Fixture::settings() const
{
  return pgm::Settings{ .biosSources = { biosDirectory() }, .romDirectory = romDirectory() };
}

} // namespace pgm::test
