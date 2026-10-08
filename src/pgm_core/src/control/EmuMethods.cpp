#include "Methods.hpp"

#include "pgm/Version.hpp"
#include "pgm/control/Describe.hpp"

#include <spdlog/fmt/fmt.h>

#include <filesystem>
#include <system_error>
#include <utility>
#include <vector>

namespace pgm::control
{

namespace
{

Error loadError( LoadFailure const& failure )
{
  switch ( failure.kind )
  {
  case LoadFailure::Kind::UNKNOWN_GAME:
    return Error{ .code = "unknown_game", .message = failure.message };
  case LoadFailure::Kind::UNKNOWN_REGION:
    return Error{ .code = "unknown_region", .message = failure.message };
  case LoadFailure::Kind::LOAD_FAILED:
    break;
  }
  return Error{ .code = "load_failed", .message = failure.message };
}

} // namespace

void addEmuMethods( Dispatcher& dispatcher, Emulator& emulator )
{
  dispatcher.add( "emu.status",
                  info( "The emulator's version, the loaded game, the region it runs as, and the emulated time: "
                        "master ticks (50 MHz) and frames since power-up." ),
                  [&emulator]( Json const& /*params*/ ) -> Outcome
                  {
                    auto const game = emulator.gameName();
                    auto const region = emulator.region();
                    Json status{ { "version", versionString() },
                                 { "game_name", game ? Json( *game ) : Json( nullptr ) },
                                 { "region", region ? Json( *region ) : Json( nullptr ) } };
                    if ( machine::Machine const* machine = emulator.machine() )
                    {
                      status["total_ticks"] = machine::masterTicks( machine->now() );
                      status["frame"] = machine->frame();
                    }
                    return status;
                  } );

  dispatcher.add(
      "emu.load_game",
      info( "Loads a game, powering a new board up: by set name from the ROM directory (pgm for the BIOS alone), or by "
            "path to a .pgm file. Give one of the two.",
            { { .name = "name",
                .type = "string",
                .description = "A set name, such as orlegend; pgm loads the BIOS alone.",
                .required = false },
              { .name = "path", .type = "string", .description = "The path of a .pgm file.", .required = false },
              { .name = "region",
                .type = "string",
                .description = "The region the game runs as: one of the codes emu.cartridge_info lists, such as WRLD "
                               "or JAPN. Without it the game runs as its image holds.",
                .required = false } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        bool const byName = params.contains( "name" );
        bool const byPath = params.contains( "path" );
        if ( byName == byPath )
        {
          return std::unexpected( badRequest( "Exactly one of name and path must be given" ) );
        }
        auto const target = requireString( params, byName ? "name" : "path" );
        if ( !target )
        {
          return std::unexpected( target.error() );
        }
        std::optional<std::string> region;
        if ( params.contains( "region" ) )
        {
          auto const code = requireString( params, "region" );
          if ( !code )
          {
            return std::unexpected( code.error() );
          }
          region = *code;
        }
        auto const loaded =
            byName ? emulator.loadGameByName( *target, region ) : emulator.loadGameFromFile( *target, region );
        if ( !loaded )
        {
          return std::unexpected( loadError( loaded.error() ) );
        }
        return Json::object();
      } );

  dispatcher.add(
      "emu.set_region",
      info( "Powers the board up again with the loaded game made another region, as loading it with that region "
            "would.",
            { { .name = "region",
                .type = "string",
                .description = "One of the codes emu.cartridge_info lists, such as WRLD or JAPN." } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const region = requireString( params, "region" );
        if ( !region )
        {
          return std::unexpected( region.error() );
        }
        if ( auto const set = emulator.setRegion( *region ); !set )
        {
          return std::unexpected( loadError( set.error() ) );
        }
        return Json::object();
      } );

  dispatcher.add(
      "emu.set_bios",
      info( "Where the BIOS files are taken from for the loads that follow: directories and zips, the first that "
            "has a file winning. The game loaded runs on undisturbed.",
            { { .name = "sources",
                .type = "array",
                .description = "Paths of directories or zips, such as the path of pgm.zip." } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        if ( !params.contains( "sources" ) || !params.at( "sources" ).is_array() || params.at( "sources" ).empty() )
        {
          return std::unexpected( badRequest( "sources must be a list of one path or more" ) );
        }
        std::vector<std::filesystem::path> sources;
        for ( Json const& source : params.at( "sources" ) )
        {
          std::error_code failed;
          if ( !source.is_string() || !std::filesystem::exists( source.get<std::string>(), failed ) )
          {
            return std::unexpected( badRequest( fmt::format( "{} is not a path that exists", source.dump() ) ) );
          }
          sources.emplace_back( source.get<std::string>() );
        }
        emulator.setBiosSources( std::move( sources ) );
        return Json::object();
      } );

  dispatcher.add( "emu.cartridge_info",
                  info( "What the loaded cartridge image holds: names, hardware class, and each ROM with its type, "
                        "size, mapping and CRC32." ),
                  [&emulator]( Json const& /*params*/ ) -> Outcome
                  {
                    cart::PgmImage const* const cartridge = emulator.cartridge();
                    if ( cartridge == nullptr )
                    {
                      return std::unexpected( Error{ .code = "no_cartridge", .message = "No cartridge is loaded" } );
                    }
                    return describe( *cartridge );
                  } );

  dispatcher.alias( "sim.status", "emu.status" );
  dispatcher.alias( "sim.load_game", "emu.load_game" );
}

} // namespace pgm::control
