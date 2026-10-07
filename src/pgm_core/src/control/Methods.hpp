#pragma once

// The methods of docs/spec/control-protocol.md, one group per file, and what
// they share for reading parameters. Private to the core: a transport sees only
// the Dispatcher.

#include "pgm/Emulator.hpp"
#include "pgm/control/Dispatcher.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace pgm::control
{

void addEmuMethods( Dispatcher& dispatcher, Emulator& emulator );
void addMemoryMethods( Dispatcher& dispatcher, Emulator& emulator );

Error badRequest( std::string message );

/// The string `params[name]`, or the `bad_request` that says it is missing or
/// is not a string.
std::expected<std::string, Error> requireString( Json const& params, std::string_view name );

/// The non-negative integer `params[name]`, or the `bad_request` that says it
/// is missing or is not one.
std::expected<std::uint64_t, Error> requireUnsigned( Json const& params, std::string_view name );

} // namespace pgm::control
