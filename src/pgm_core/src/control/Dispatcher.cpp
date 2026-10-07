#include "pgm/control/Dispatcher.hpp"

#include "pgm/Version.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace pgm::control
{

namespace
{

Json errorResponse( std::uint64_t id, Error const& error )
{
  return Json{ { "id", id }, { "ok", false }, { "error", { { "code", error.code }, { "message", error.message } } } };
}

Json resultResponse( std::uint64_t id, Json result )
{
  return Json{ { "id", id }, { "ok", true }, { "result", std::move( result ) } };
}

Error badRequest( std::string message )
{
  return Error{ .code = "bad_request", .message = std::move( message ) };
}

Outcome status( Json const& /*params*/ )
{
  return Json{ { "version", versionString() } };
}

} // namespace

Dispatcher::Dispatcher()
{
  add( "emu.status", status );

  alias( "sim.status", "emu.status" );
}

Json Dispatcher::handle( Json const& request ) const
{
  // The id is read first, so that every later failure can be answered under
  // it. Until it is known the protocol answers under 0, as the RTL simulator
  // does.
  if ( !request.is_object() )
  {
    return errorResponse( 0, badRequest( "Request must be a JSON object" ) );
  }

  // Any non-negative integer is an id. Parsed text holds one as unsigned, but a
  // request built in C++ from an `int` holds it as signed, so the sign of the
  // value is what is checked, not how it is stored.
  auto const idField = request.find( "id" );
  if ( idField == request.end() || !idField->is_number_integer() ||
       ( !idField->is_number_unsigned() && idField->get<std::int64_t>() < 0 ) )
  {
    return errorResponse( 0, badRequest( "Missing or invalid field: id" ) );
  }
  auto const id = idField->get<std::uint64_t>();

  auto const methodField = request.find( "method" );
  if ( methodField == request.end() || !methodField->is_string() )
  {
    return errorResponse( id, badRequest( "Missing or invalid field: method" ) );
  }
  auto const& method = methodField->get_ref<std::string const&>();

  // `params` may be left out; a method that takes none sees an empty object.
  static Json const EMPTY_PARAMS = Json::object();
  auto const paramsField = request.find( "params" );
  bool const hasParams = paramsField != request.end();
  if ( hasParams && !paramsField->is_object() )
  {
    return errorResponse( id, badRequest( "Field params must be an object" ) );
  }

  auto const handler = mHandlers.find( method );
  if ( handler == mHandlers.end() )
  {
    return errorResponse( id, Error{ .code = "unknown_method", .message = "Unknown method: " + method } );
  }

  auto outcome = handler->second( hasParams ? *paramsField : EMPTY_PARAMS );
  if ( !outcome )
  {
    return errorResponse( id, outcome.error() );
  }
  return resultResponse( id, std::move( *outcome ) );
}

std::vector<std::string> Dispatcher::methodNames() const
{
  std::vector<std::string> names;
  names.reserve( mHandlers.size() );
  for ( auto const& [name, handler] : mHandlers )
  {
    names.push_back( name );
  }
  std::ranges::sort( names );
  return names;
}

void Dispatcher::add( std::string_view name, Handler handler )
{
  mHandlers.emplace( name, std::move( handler ) );
}

void Dispatcher::alias( std::string_view alias, std::string_view name )
{
  mHandlers.emplace( alias, mHandlers.at( std::string{ name } ) );
}

} // namespace pgm::control
