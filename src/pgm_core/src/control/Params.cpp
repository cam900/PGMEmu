#include "Methods.hpp"

#include <spdlog/fmt/fmt.h>

#include <utility>

namespace pgm::control
{

Error badRequest( std::string message )
{
  return Error{ .code = "bad_request", .message = std::move( message ) };
}

std::expected<std::string, Error> requireString( Json const& params, std::string_view name )
{
  auto const field = params.find( name );
  if ( field == params.end() || !field->is_string() )
  {
    return std::unexpected( badRequest( fmt::format( "Missing or invalid field: {}", name ) ) );
  }
  return field->get<std::string>();
}

std::expected<std::uint64_t, Error> requireUnsigned( Json const& params, std::string_view name )
{
  // Parsed text holds a non-negative integer as unsigned, a request built in
  // C++ from an `int` holds it as signed; the value is what is checked.
  auto const field = params.find( name );
  if ( field == params.end() || !field->is_number_integer() ||
       ( !field->is_number_unsigned() && field->get<std::int64_t>() < 0 ) )
  {
    return std::unexpected( badRequest( fmt::format( "Missing or invalid field: {}", name ) ) );
  }
  return field->get<std::uint64_t>();
}

} // namespace pgm::control
