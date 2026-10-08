#pragma once

#include "pgm/control/Dispatcher.hpp"

#include <SDL3/SDL_gamepad.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pgm::app
{

/// A player's controls on the board.
enum class Control : std::uint8_t
{
  UP,
  DOWN,
  LEFT,
  RIGHT,
  BUTTON_1,
  BUTTON_2,
  BUTTON_3,
  BUTTON_4,
  START,
  COIN
};

inline constexpr std::size_t CONTROLS = 10;
inline constexpr std::size_t PLAYERS = 4;

/// The control's name as the input window shows it.
[[nodiscard]] std::string_view labelOf( Control control );

/// A key or a gamepad's input that a control is bound to. A gamepad's binding
/// is to the gamepad of the player whose control it is.
struct Binding
{
  enum class Kind : std::uint8_t
  {
    KEY,
    BUTTON,
    AXIS
  };

  Kind kind{};
  /// An SDL_Scancode, SDL_GamepadButton or SDL_GamepadAxis.
  int code{};
  /// For an axis: -1 when pushed towards its negative end, +1 towards its
  /// positive.
  int direction{};

  bool operator==( Binding const& ) const = default;
};

/// What `binding` is, as the input window shows it: "Z", "Gamepad A", "Gamepad
/// left stick -X".
[[nodiscard]] std::string describe( Binding const& binding );

/// Which key and gamepad input is which player's control, and the board's
/// input words they make (PGM.sv's IN0..IN3). Kept with the user's
/// preferences as JSON.
class InputMap
{
public:
  struct Player
  {
    std::array<std::vector<Binding>, CONTROLS> bindings;
    /// Which of the connected gamepads, in the order they were connected, is
    /// the player's; none when negative.
    int gamepad{ -1 };
  };

  /// Player 1 on the arrows, Z, X, C, V, with 1 to start and 5 for a coin;
  /// player 2 on R, F, D, G, A, S, Q, W, 2 and 6, as MAME has them; and each
  /// player on the gamepad connected in its turn.
  static InputMap defaults();

  [[nodiscard]] std::array<Player, PLAYERS>& players();
  [[nodiscard]] std::array<Player, PLAYERS> const& players() const;

  /// IN0..IN3 with a bit set for each control held: on `keys`, SDL's keyboard
  /// state, unless it is null, and on each player's gamepad in `connected`.
  [[nodiscard]] std::array<std::uint16_t, 4> pressed( bool const* keys,
                                                      std::vector<SDL_Gamepad*> const& connected ) const;

  [[nodiscard]] control::Json toJson() const;
  /// The map `json` describes, or nothing when it is not one; bindings it
  /// cannot read are left out.
  static std::optional<InputMap> fromJson( control::Json const& json );

  /// The map saved at `path`, or the defaults when there is none to read.
  static InputMap load( std::filesystem::path const& path );
  void save( std::filesystem::path const& path ) const;

private:
  std::array<Player, PLAYERS> mPlayers;
};

} // namespace pgm::app
