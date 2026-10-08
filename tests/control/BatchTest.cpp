#include <catch2/catch_test_macros.hpp>

#include "support/Fixture.hpp"

#include "pgm/control/Batch.hpp"

#include <string>

using pgm::control::BatchResult;
using pgm::control::Json;
using pgm::control::runBatch;
using pgm::test::Fixture;

namespace
{

Json script( Json steps )
{
  return Json{ { "steps", std::move( steps ) } };
}

Json load()
{
  return Json{ { "method", "emu.load_game" }, { "params", { { "name", "pgm" } } } };
}

Json run()
{
  return Json{ { "method", "emu.run_frames" }, { "params", { { "count", 2 } } } };
}

} // namespace

TEST_CASE( "a batch script runs its requests, and sums the machine at each checkpoint", "[control][batch]" )
{
  Fixture const fixture;
  auto const result = runBatch(
      fixture.settings(),
      script( Json::array( { load(), run(), { { "checkpoint", "a" } }, run(), { { "checkpoint", "b" } } } ) ) );

  REQUIRE( result.outcome == BatchResult::Outcome::RAN );
  REQUIRE( result.checkpoints.size() == 2 );
  auto const& sums = result.checkpoints[0].sums;
  REQUIRE( result.checkpoints[0].name == "a" );
  REQUIRE( sums.at( "frame" ) == 2 );
  for ( char const* key : { "picture", "work_ram", "video_ram", "palette_ram", "audio_ram", "sound" } )
  {
    REQUIRE( sums.at( key ).get<std::string>().size() == 8 );
  }
  REQUIRE( result.checkpoints[1].sums.at( "frame" ) == 4 );

  // The same script sums the same; the sums are the golden ones then.
  auto const again = runBatch(
      fixture.settings(),
      script( Json::array( { load(), run(), { { "checkpoint", "a" } }, run(), { { "checkpoint", "b" } } } ) ) );
  REQUIRE( compareWithGolden( again, goldenOf( result ) ).empty() );
}

TEST_CASE( "a checkpoint that differs from its golden sums is reported, and so is one never reached",
           "[control][batch]" )
{
  Fixture const fixture;
  auto const result =
      runBatch( fixture.settings(), script( Json::array( { load(), run(), { { "checkpoint", "a" } } } ) ) );
  Json golden = goldenOf( result );
  golden["a"]["frame"] = 99;
  golden["later"] = golden["a"];

  auto const differences = compareWithGolden( result, golden );

  REQUIRE( differences.size() == 2 );
  REQUIRE( differences[0] == "a: frame is 2, golden 99" );
  REQUIRE( differences[1] == "later: not reached" );
}

TEST_CASE( "a script whose game or program is not there is missing; a failing step fails it", "[control][batch]" )
{
  Fixture const fixture;

  auto const unknown = runBatch(
      fixture.settings(),
      script( Json::array( { { { "method", "emu.load_game" }, { "params", { { "name", "nosuchgame" } } } } } ) ) );
  REQUIRE( unknown.outcome == BatchResult::Outcome::MISSING );

  Json withProgram = script( Json::array( { load() } ) );
  withProgram["bios_program"] = ( fixture.romDirectory() / "none" ).string();
  REQUIRE( runBatch( fixture.settings(), withProgram ).outcome == BatchResult::Outcome::MISSING );

  auto const failing =
      runBatch( fixture.settings(), script( Json::array( { load(), { { "method", "emu.run_frames" } } } ) ) );
  REQUIRE( failing.outcome == BatchResult::Outcome::FAILED );
  REQUIRE( failing.message.starts_with( "step 2 (emu.run_frames): bad_request" ) );

  REQUIRE( runBatch( fixture.settings(), Json::object() ).outcome == BatchResult::Outcome::FAILED );
  REQUIRE( runBatch( fixture.settings(), script( Json::array( { { { "checkpoint", "early" } } } ) ) ).outcome ==
           BatchResult::Outcome::FAILED );
}
