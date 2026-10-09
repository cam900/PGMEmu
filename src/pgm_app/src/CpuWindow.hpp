#pragma once

#include "pgm/control/Dispatcher.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pgm::app
{

/// The input map's editor: a tab a player, each control's bindings with a way
/// to add one by pressing it and to clear them, and which gamepad is whose;
/// and a tab of the hotkeys, bound the same way.
class CpuWindow
{
public:  /// Answers a request of the control protocol: `method` with `params`.
  using Request = std::function<control::Json( std::string const& method, control::Json params )>;

  CpuWindow( Request request );

  /// Lays the window out while `open`, which its close button clears.
  void draw( bool& open );

private:
  Request mRequest;
};

} // namespace pgm::app
