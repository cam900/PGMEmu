#pragma once

#include <nlohmann/json.hpp>

#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pgm::control
{

/// Keys keep the order they were written in, so a response reads `id`, `ok`,
/// `result` as the RTL simulator's does, and transcripts of the two can be
/// compared as text.
using Json = nlohmann::ordered_json;

/// Why a request failed, as the protocol reports it. `code` is one of the
/// stable identifiers listed in docs/spec/control-protocol.md; `message` is for
/// a person.
struct Error
{
  std::string code;
  std::string message;
};

/// What a method answers: the `result` object, or the error that replaces it.
using Outcome = std::expected<Json, Error>;

/// The one place every capability of the emulator is reached through, whatever
/// the transport (docs/decisions/0005-one-control-api.md). It takes a request
/// object and answers a response object; framing, sockets and MCP belong to the
/// transports in pgm_server.
class Dispatcher
{
public:
  Dispatcher();

  /// Answers one request. Never throws and always answers: a malformed request
  /// is answered with a `bad_request` error, not refused.
  [[nodiscard]] Json handle( Json const& request ) const;

  /// Every method name a request may use, aliases included, in a stable order.
  [[nodiscard]] std::vector<std::string> methodNames() const;

private:
  using Handler = std::function<Outcome( Json const& params )>;

  void add( std::string_view name, Handler handler );

  /// Makes `alias` answer exactly as `name` does. The RTL simulator's `sim.*`
  /// names are aliases of the emulator's `emu.*`, so one script drives both.
  void alias( std::string_view alias, std::string_view name );

  std::unordered_map<std::string, Handler> mHandlers;
};

} // namespace pgm::control
