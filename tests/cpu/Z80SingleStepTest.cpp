#include <catch2/catch_test_macros.hpp>

#include "machine/Z80.hpp"
#include "pgm/control/Dispatcher.hpp"
#include "support/Files.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// The SingleStepTests Z80 suite (https://github.com/SingleStepTests/z80), run
// against the Z80 adapter: every opcode from a random state, compared with the
// final state, the memory, the ports written and the cycle count.
// scripts/fetch-z80-tests.sh downloads the suite; without it these skip.

using Json = nlohmann::json;
using pgm::machine::Z80;
using pgm::machine::Z80Bus;
using pgm::machine::Z80Registers;

namespace
{

// Where z80.h and the suite disagree, as measured on suite commit ebe1875.
// Every case is undocumented behaviour z80.h does not model, and none is one a
// sound driver can see: flags 3 and 5 after SCF and CCF, which depend on the Q
// register; the flags a repeating block instruction leaves on each pass; and
// HALT, where z80.h keeps PC on the HALT and the suite one past it. The counts
// are held so that a change to z80.h that moves any of them is noticed.
std::map<std::string, std::size_t> const& knownDisagreements()
{
  static std::map<std::string, std::size_t> const COUNTS{
    { "37.json.gz", 229 },     { "3f.json.gz", 219 },     { "76.json.gz", 1000 },   { "dd 37.json.gz", 454 },
    { "dd 3f.json.gz", 440 },  { "dd 76.json.gz", 1000 }, { "ed b0.json.gz", 749 }, { "ed b1.json.gz", 727 },
    { "ed b2.json.gz", 930 },  { "ed b3.json.gz", 925 },  { "ed b8.json.gz", 721 }, { "ed b9.json.gz", 762 },
    { "ed ba.json.gz", 931 },  { "ed bb.json.gz", 937 },  { "fd 37.json.gz", 456 }, { "fd 3f.json.gz", 412 },
    { "fd 76.json.gz", 1000 },
  };
  return COUNTS;
}

std::filesystem::path suiteDirectory()
{
  return PGM_TEST_Z80_DIR;
}

/// A Z80 on 64 KB of RAM, with the port reads a test gives it.
class TestBus final : public Z80Bus
{
public:
  std::array<std::uint8_t, 0x10000> memory{};
  std::vector<std::pair<std::uint16_t, std::uint8_t>> portReads;
  std::vector<std::pair<std::uint16_t, std::uint8_t>> portWrites;

  std::uint8_t read( std::uint16_t address ) override
  {
    return memory.at( address );
  }

  void write( std::uint16_t address, std::uint8_t value ) override
  {
    memory.at( address ) = value;
  }

  std::uint8_t in( std::uint16_t port ) override
  {
    auto const found = std::ranges::find( portReads, port, &std::pair<std::uint16_t, std::uint8_t>::first );
    if ( found == portReads.end() )
    {
      return 0xff;
    }
    std::uint8_t const value = found->second;
    portReads.erase( found );
    return value;
  }

  void out( std::uint16_t port, std::uint8_t value ) override
  {
    portWrites.emplace_back( port, value );
  }

  std::uint8_t acknowledge( std::uint16_t /*address*/ ) override
  {
    return 0xff;
  }
};

std::uint16_t pair( Json const& state, char const* high, char const* low )
{
  return static_cast<std::uint16_t>( ( state.at( high ).get<unsigned>() << 8U ) | state.at( low ).get<unsigned>() );
}

Z80Registers registersOf( Json const& state )
{
  return Z80Registers{ .af = pair( state, "a", "f" ),
                       .bc = pair( state, "b", "c" ),
                       .de = pair( state, "d", "e" ),
                       .hl = pair( state, "h", "l" ),
                       .ix = state.at( "ix" ).get<std::uint16_t>(),
                       .iy = state.at( "iy" ).get<std::uint16_t>(),
                       .sp = state.at( "sp" ).get<std::uint16_t>(),
                       .pc = state.at( "pc" ).get<std::uint16_t>(),
                       .wz = state.at( "wz" ).get<std::uint16_t>(),
                       .af2 = state.at( "af_" ).get<std::uint16_t>(),
                       .bc2 = state.at( "bc_" ).get<std::uint16_t>(),
                       .de2 = state.at( "de_" ).get<std::uint16_t>(),
                       .hl2 = state.at( "hl_" ).get<std::uint16_t>(),
                       .i = state.at( "i" ).get<std::uint8_t>(),
                       .r = state.at( "r" ).get<std::uint8_t>(),
                       .im = state.at( "im" ).get<std::uint8_t>(),
                       .iff1 = state.at( "iff1" ).get<int>() != 0,
                       .iff2 = state.at( "iff2" ).get<int>() != 0 };
}

/// Runs the test's one instruction, and returns how many T-states it took.
std::size_t execute( Z80& z80, TestBus& bus, Json const& test )
{
  bus.memory.fill( 0 );
  for ( auto const& cell : test.at( "initial" ).at( "ram" ) )
  {
    bus.memory.at( cell.at( 0 ).get<std::uint16_t>() ) = cell.at( 1 ).get<std::uint8_t>();
  }
  bus.portReads.clear();
  bus.portWrites.clear();
  if ( test.contains( "ports" ) )
  {
    for ( auto const& port : test.at( "ports" ) )
    {
      if ( port.at( 2 ).get<std::string>() == "r" )
      {
        bus.portReads.emplace_back( port.at( 0 ).get<std::uint16_t>(), port.at( 1 ).get<std::uint8_t>() );
      }
    }
  }

  z80.setRegisters( registersOf( test.at( "initial" ) ) );
  // The first tick is the instruction's first T-state, its opcode fetch. z80.h
  // overlaps the end of an instruction with the next fetch, so the
  // instruction is complete when the next fetch has begun.
  z80.tick( bus );
  std::size_t ticks = 0;
  do
  {
    z80.tick( bus );
    ++ticks;
  } while ( !z80.instructionBoundary() && ticks < 1000 );
  return ticks;
}

/// What differs between the Z80 after the instruction and the test's final
/// state, or nothing.
std::optional<std::string> mismatch( Z80 const& z80, TestBus const& bus, Json const& test, std::size_t ticks )
{
  Json const& expected = test.at( "final" );
  Z80Registers actual = z80.registers();
  // The next fetch has taken its opcode's address.
  actual.pc = static_cast<std::uint16_t>( actual.pc - 1 );
  Z80Registers const wanted = registersOf( expected );

  std::ostringstream out;
  auto const check = [&]( char const* name, unsigned have, unsigned want )
  {
    if ( have != want )
    {
      out << name << " " << std::hex << have << " != " << want << "; ";
    }
  };
  check( "af", actual.af, wanted.af );
  check( "bc", actual.bc, wanted.bc );
  check( "de", actual.de, wanted.de );
  check( "hl", actual.hl, wanted.hl );
  check( "ix", actual.ix, wanted.ix );
  check( "iy", actual.iy, wanted.iy );
  check( "sp", actual.sp, wanted.sp );
  check( "pc", actual.pc, wanted.pc );
  check( "wz", actual.wz, wanted.wz );
  check( "af'", actual.af2, wanted.af2 );
  check( "bc'", actual.bc2, wanted.bc2 );
  check( "de'", actual.de2, wanted.de2 );
  check( "hl'", actual.hl2, wanted.hl2 );
  check( "i", actual.i, wanted.i );
  check( "r", actual.r, wanted.r );
  check( "im", actual.im, wanted.im );
  check( "iff1", actual.iff1 ? 1U : 0U, wanted.iff1 ? 1U : 0U );
  check( "iff2", actual.iff2 ? 1U : 0U, wanted.iff2 ? 1U : 0U );
  for ( auto const& cell : expected.at( "ram" ) )
  {
    auto const address = cell.at( 0 ).get<std::uint16_t>();
    check( "ram", bus.memory.at( address ), cell.at( 1 ).get<unsigned>() );
  }
  std::vector<std::pair<std::uint16_t, std::uint8_t>> written;
  if ( test.contains( "ports" ) )
  {
    for ( auto const& port : test.at( "ports" ) )
    {
      if ( port.at( 2 ).get<std::string>() == "w" )
      {
        written.emplace_back( port.at( 0 ).get<std::uint16_t>(), port.at( 1 ).get<std::uint8_t>() );
      }
    }
  }
  if ( written != bus.portWrites )
  {
    out << "port writes differ; ";
  }
  check( "cycles", static_cast<unsigned>( ticks ), static_cast<unsigned>( test.at( "cycles" ).size() ) );
  std::string const text = out.str();
  return text.empty() ? std::nullopt : std::optional<std::string>{ text };
}

} // namespace

TEST_CASE( "the Z80 passes the SingleStepTests Z80 suite", "[cpu-suite]" )
{
  std::vector<std::filesystem::path> files;
  std::error_code failed;
  for ( auto const& entry : std::filesystem::directory_iterator( suiteDirectory(), failed ) )
  {
    if ( entry.path().string().ends_with( ".json.gz" ) )
    {
      files.push_back( entry.path() );
    }
  }
  if ( files.empty() )
  {
    SKIP( "no suite in " << suiteDirectory() << "; fetch it with scripts/fetch-z80-tests.sh" );
  }
  std::ranges::sort( files );

  Z80 z80;
  TestBus bus;
  std::ostringstream report;
  std::size_t total = 0;
  std::size_t passed = 0;
  bool unexpected = false;
  for ( auto const& file : files )
  {
    Json const tests = Json::parse( pgm::test::readGzip( file ) );
    std::size_t failedHere = 0;
    std::string firstFailure;
    for ( auto const& test : tests )
    {
      std::size_t const ticks = execute( z80, bus, test );
      auto const problem = mismatch( z80, bus, test, ticks );
      ++total;
      if ( problem )
      {
        if ( failedHere++ == 0 )
        {
          firstFailure = test.at( "name" ).get<std::string>() + ": " + *problem;
        }
      }
      else
      {
        ++passed;
      }
    }
    auto const known = knownDisagreements().find( file.filename().string() );
    std::size_t const expected = known == knownDisagreements().end() ? 0 : known->second;
    if ( failedHere != expected )
    {
      report << file.filename().string() << ": " << failedHere << " of " << tests.size() << " fail where " << expected
             << " are known to, first " << firstFailure << "\n";
      unexpected = true;
    }
  }
  INFO( report.str() << passed << " of " << total << " pass" );
  CHECK_FALSE( unexpected );
}
