#pragma once

#include "pgm/control/Dispatcher.hpp"

#include <iosfwd>
#include <optional>
#include <string>
#include <unordered_map>

namespace pgm::server
{

/// The Model Context Protocol onto the dispatcher: JSON-RPC 2.0 messages, a
/// tool per method of the dispatcher, made from its method table
/// (docs/decisions/0005-one-control-api.md). A tool is named as its method with
/// underscores for the dots, emu_run_frames for emu.run_frames, as MCP allows
/// no dots in a name. A screenshot comes back as an image the agent can see.
class McpServer
{
public:
  explicit McpServer( control::Dispatcher const& dispatcher );

  /// The stdio transport: a message per line in, a message per line out, until
  /// `in` ends.
  void serve( std::istream& in, std::ostream& out );

  /// Answers one message, or nothing for a notification.
  [[nodiscard]] std::optional<control::Json> handle( control::Json const& message );

  /// Answers one line of text, or nothing for a notification. Text that is not
  /// JSON is a parse error.
  [[nodiscard]] std::optional<std::string> handleLine( std::string const& line );

private:
  [[nodiscard]] control::Json listTools() const;
  [[nodiscard]] std::optional<control::Json> callTool( control::Json const& params, control::Json& error ) const;

  control::Dispatcher const& mDispatcher;
  /// Tool name to method name.
  std::unordered_map<std::string, std::string> mTools;
};

} // namespace pgm::server
