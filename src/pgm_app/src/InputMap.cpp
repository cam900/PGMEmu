#include "InputMap.hpp"

#include <SDL3/SDL_keyboard.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <utility>

namespace pgm::app
{

namespace
{

/// How far a stick is pushed before it counts as a direction held: half way.
constexpr int AXIS_THRESHOLD = 16384;

struct ControlName
{
  std::string_view key;
  std::string_view label;
};

// In Control's order.
constexpr std::array<ControlName, CONTROLS> NAMES{ { { .key = "up", .label = "Up" },
                                                     { .key = "down", .label = "Down" },
                                                     { .key = "left", .label = "Left" },
                                                     { .key = "right", .label = "Right" },
                                                     { .key = "button1", .label = "Button 1" },
                                                     { .key = "button2", .label = "Button 2" },
                                                     { .key = "button3", .label = "Button 3" },
                                                     { .key = "button4", .label = "Button 4" },
                                                     { .key = "start", .label = "Start" },
                                                     { .key = "coin", .label = "Coin" } } };

// In Hotkey's order.
constexpr std::array<ControlName, HOTKEYS> HOTKEY_NAMES{ { { .key = "rewind", .label = "Rewind" } } };

/// Where a player's control is in IN0..IN3 (PGM.sv): players 1 and 2 have the
/// two bytes of IN0, players 3 and 4 those of IN1, each start, up, down, left,
/// right and buttons 1-3 from its bit 0; IN2 holds the coins in its low nibble
/// and the buttons 4 from bit 8.
std::pair<std::size_t, std::uint16_t> bitOf( std::size_t player, Control control )
{
  std::size_t const word = player / 2;
  unsigned const shift = static_cast<unsigned>( player % 2 ) * 8;
  auto const in = [&]( unsigned bit )
  { return std::pair{ word, static_cast<std::uint16_t>( 1U << ( bit + shift ) ) }; };
  switch ( control )
  {
  case Control::START:
    return in( 0 );
  case Control::UP:
    return in( 1 );
  case Control::DOWN:
    return in( 2 );
  case Control::LEFT:
    return in( 3 );
  case Control::RIGHT:
    return in( 4 );
  case Control::BUTTON_1:
    return in( 5 );
  case Control::BUTTON_2:
    return in( 6 );
  case Control::BUTTON_3:
    return in( 7 );
  case Control::BUTTON_4:
    return { 2, static_cast<std::uint16_t>( 1U << ( 8 + player ) ) };
  case Control::COIN:
    return { 2, static_cast<std::uint16_t>( 1U << player ) };
  }
  return { 0, 0 };
}

Binding key( SDL_Scancode code )
{
  return Binding{ .kind = Binding::Kind::KEY, .code = code, .direction = 0 };
}

Binding button( SDL_GamepadButton code )
{
  return Binding{ .kind = Binding::Kind::BUTTON, .code = code, .direction = 0 };
}

Binding axis( SDL_GamepadAxis code, int direction )
{
  return Binding{ .kind = Binding::Kind::AXIS, .code = code, .direction = direction };
}

std::vector<Binding>& bindingsOf( InputMap::Player& player, Control control )
{
  return player.bindings.at( static_cast<std::size_t>( control ) );
}

std::array<std::vector<Binding>, HOTKEYS> defaultHotkeys()
{
  return { { { key( SDL_SCANCODE_BACKSPACE ), button( SDL_GAMEPAD_BUTTON_LEFT_SHOULDER ) } } };
}

/// Whether `binding` is held on `keys`, unless it is null, or on `gamepad`,
/// unless it is null.
bool isHeld( Binding const& binding, bool const* keys, SDL_Gamepad* gamepad )
{
  switch ( binding.kind )
  {
  case Binding::Kind::KEY:
    return keys != nullptr && keys[binding.code];
  case Binding::Kind::BUTTON:
    return gamepad != nullptr && SDL_GetGamepadButton( gamepad, static_cast<SDL_GamepadButton>( binding.code ) );
  case Binding::Kind::AXIS:
    return gamepad != nullptr &&
           SDL_GetGamepadAxis( gamepad, static_cast<SDL_GamepadAxis>( binding.code ) ) * binding.direction >
               AXIS_THRESHOLD;
  }
  return false;
}

/// A binding as the JSON holds it: "key:Z", "button:a", "axis:-leftx".
std::string encode( Binding const& binding )
{
  switch ( binding.kind )
  {
  case Binding::Kind::KEY:
    return fmt::format( "key:{}", SDL_GetScancodeName( static_cast<SDL_Scancode>( binding.code ) ) );
  case Binding::Kind::BUTTON:
    return fmt::format( "button:{}", SDL_GetGamepadStringForButton( static_cast<SDL_GamepadButton>( binding.code ) ) );
  case Binding::Kind::AXIS:
    return fmt::format( "axis:{}{}",
                        binding.direction < 0 ? '-' : '+',
                        SDL_GetGamepadStringForAxis( static_cast<SDL_GamepadAxis>( binding.code ) ) );
  }
  return {};
}

std::optional<Binding> decode( std::string_view text )
{
  std::size_t const colon = text.find( ':' );
  if ( colon == std::string_view::npos )
  {
    return std::nullopt;
  }
  std::string_view const kind = text.substr( 0, colon );
  std::string const name{ text.substr( colon + 1 ) };
  if ( kind == "key" )
  {
    SDL_Scancode const code = SDL_GetScancodeFromName( name.c_str() );
    return code == SDL_SCANCODE_UNKNOWN ? std::nullopt : std::optional{ key( code ) };
  }
  if ( kind == "button" )
  {
    SDL_GamepadButton const code = SDL_GetGamepadButtonFromString( name.c_str() );
    return code == SDL_GAMEPAD_BUTTON_INVALID ? std::nullopt : std::optional{ button( code ) };
  }
  if ( kind == "axis" && name.size() > 1 && ( name[0] == '-' || name[0] == '+' ) )
  {
    SDL_GamepadAxis const code = SDL_GetGamepadAxisFromString( name.c_str() + 1 );
    return code == SDL_GAMEPAD_AXIS_INVALID ? std::nullopt : std::optional{ axis( code, name[0] == '-' ? -1 : 1 ) };
  }
  return std::nullopt;
}

/// A list of bindings as the JSON holds it.
control::Json encodeAll( std::vector<Binding> const& bindings )
{
  control::Json list = control::Json::array();
  for ( Binding const& binding : bindings )
  {
    list.push_back( encode( binding ) );
  }
  return list;
}

/// The bindings `list` holds, those it cannot read left out.
std::vector<Binding> decodeAll( control::Json const& list )
{
  std::vector<Binding> bindings;
  for ( control::Json const& text : list )
  {
    if ( auto const binding = text.is_string() ? decode( text.get<std::string>() ) : std::nullopt )
    {
      bindings.push_back( *binding );
    }
  }
  return bindings;
}

} // namespace

std::string_view labelOf( Control control )
{
  return NAMES.at( static_cast<std::size_t>( control ) ).label;
}

std::string_view labelOf( Hotkey hotkey )
{
  return HOTKEY_NAMES.at( static_cast<std::size_t>( hotkey ) ).label;
}

std::string describe( Binding const& binding )
{
  switch ( binding.kind )
  {
  case Binding::Kind::KEY:
    return SDL_GetScancodeName( static_cast<SDL_Scancode>( binding.code ) );
  case Binding::Kind::BUTTON:
    return fmt::format( "Gamepad {}", SDL_GetGamepadStringForButton( static_cast<SDL_GamepadButton>( binding.code ) ) );
  case Binding::Kind::AXIS:
    return fmt::format( "Gamepad {}{}",
                        binding.direction < 0 ? '-' : '+',
                        SDL_GetGamepadStringForAxis( static_cast<SDL_GamepadAxis>( binding.code ) ) );
  }
  return {};
}

InputMap InputMap::defaults()
{
  InputMap map;
  auto& players = map.mPlayers;
  auto const keys = [&]( std::size_t player, std::initializer_list<std::pair<Control, SDL_Scancode>> pairs )
  {
    for ( auto const& [control, code] : pairs )
    {
      bindingsOf( players.at( player ), control ).push_back( key( code ) );
    }
  };
  keys( 0,
        { { Control::UP, SDL_SCANCODE_UP },
          { Control::DOWN, SDL_SCANCODE_DOWN },
          { Control::LEFT, SDL_SCANCODE_LEFT },
          { Control::RIGHT, SDL_SCANCODE_RIGHT },
          { Control::BUTTON_1, SDL_SCANCODE_Z },
          { Control::BUTTON_2, SDL_SCANCODE_X },
          { Control::BUTTON_3, SDL_SCANCODE_C },
          { Control::BUTTON_4, SDL_SCANCODE_V },
          { Control::START, SDL_SCANCODE_1 },
          { Control::COIN, SDL_SCANCODE_5 } } );
  keys( 1,
        { { Control::UP, SDL_SCANCODE_R },
          { Control::DOWN, SDL_SCANCODE_F },
          { Control::LEFT, SDL_SCANCODE_D },
          { Control::RIGHT, SDL_SCANCODE_G },
          { Control::BUTTON_1, SDL_SCANCODE_A },
          { Control::BUTTON_2, SDL_SCANCODE_S },
          { Control::BUTTON_3, SDL_SCANCODE_Q },
          { Control::BUTTON_4, SDL_SCANCODE_W },
          { Control::START, SDL_SCANCODE_2 },
          { Control::COIN, SDL_SCANCODE_6 } } );
  for ( std::size_t i = 0; i < PLAYERS; ++i )
  {
    Player& player = players.at( i );
    player.gamepad = static_cast<int>( i );
    for ( auto const& [control, bindings] : std::initializer_list<std::pair<Control, std::vector<Binding>>>{
              { Control::UP, { button( SDL_GAMEPAD_BUTTON_DPAD_UP ), axis( SDL_GAMEPAD_AXIS_LEFTY, -1 ) } },
              { Control::DOWN, { button( SDL_GAMEPAD_BUTTON_DPAD_DOWN ), axis( SDL_GAMEPAD_AXIS_LEFTY, 1 ) } },
              { Control::LEFT, { button( SDL_GAMEPAD_BUTTON_DPAD_LEFT ), axis( SDL_GAMEPAD_AXIS_LEFTX, -1 ) } },
              { Control::RIGHT, { button( SDL_GAMEPAD_BUTTON_DPAD_RIGHT ), axis( SDL_GAMEPAD_AXIS_LEFTX, 1 ) } },
              { Control::BUTTON_1, { button( SDL_GAMEPAD_BUTTON_SOUTH ) } },
              { Control::BUTTON_2, { button( SDL_GAMEPAD_BUTTON_EAST ) } },
              { Control::BUTTON_3, { button( SDL_GAMEPAD_BUTTON_WEST ) } },
              { Control::BUTTON_4, { button( SDL_GAMEPAD_BUTTON_NORTH ) } },
              { Control::START, { button( SDL_GAMEPAD_BUTTON_START ) } },
              { Control::COIN, { button( SDL_GAMEPAD_BUTTON_BACK ) } } } )
    {
      auto& list = bindingsOf( player, control );
      list.insert( list.end(), bindings.begin(), bindings.end() );
    }
  }
  map.mHotkeys = defaultHotkeys();
  return map;
}

std::array<InputMap::Player, PLAYERS>& InputMap::players()
{
  return mPlayers;
}

std::array<InputMap::Player, PLAYERS> const& InputMap::players() const
{
  return mPlayers;
}

std::array<std::vector<Binding>, HOTKEYS>& InputMap::hotkeys()
{
  return mHotkeys;
}

std::array<std::vector<Binding>, HOTKEYS> const& InputMap::hotkeys() const
{
  return mHotkeys;
}

std::array<std::uint16_t, 4> InputMap::pressed( bool const* keys, std::vector<SDL_Gamepad*> const& connected ) const
{
  std::array<std::uint16_t, 4> words{};
  for ( std::size_t p = 0; p < PLAYERS; ++p )
  {
    Player const& player = mPlayers.at( p );
    SDL_Gamepad* const gamepad = player.gamepad >= 0 && static_cast<std::size_t>( player.gamepad ) < connected.size()
                                     ? connected.at( static_cast<std::size_t>( player.gamepad ) )
                                     : nullptr;
    for ( std::size_t c = 0; c < CONTROLS; ++c )
    {
      for ( Binding const& binding : player.bindings.at( c ) )
      {
        if ( isHeld( binding, keys, gamepad ) )
        {
          auto const [word, bit] = bitOf( p, static_cast<Control>( c ) );
          words.at( word ) = static_cast<std::uint16_t>( words.at( word ) | bit );
          break;
        }
      }
    }
  }
  return words;
}

bool InputMap::held( Hotkey hotkey, bool const* keys, std::vector<SDL_Gamepad*> const& connected ) const
{
  for ( Binding const& binding : mHotkeys.at( static_cast<std::size_t>( hotkey ) ) )
  {
    if ( binding.kind == Binding::Kind::KEY
             ? isHeld( binding, keys, nullptr )
             : std::ranges::any_of( connected,
                                    [&]( SDL_Gamepad* gamepad ) { return isHeld( binding, nullptr, gamepad ); } ) )
    {
      return true;
    }
  }
  return false;
}

control::Json InputMap::toJson() const
{
  control::Json players = control::Json::array();
  for ( Player const& player : mPlayers )
  {
    control::Json bindings = control::Json::object();
    for ( std::size_t c = 0; c < CONTROLS; ++c )
    {
      bindings[std::string{ NAMES.at( c ).key }] = encodeAll( player.bindings.at( c ) );
    }
    players.push_back( control::Json{ { "gamepad", player.gamepad }, { "bindings", std::move( bindings ) } } );
  }
  control::Json hotkeys = control::Json::object();
  for ( std::size_t h = 0; h < HOTKEYS; ++h )
  {
    hotkeys[std::string{ HOTKEY_NAMES.at( h ).key }] = encodeAll( mHotkeys.at( h ) );
  }
  return control::Json{ { "players", std::move( players ) }, { "hotkeys", std::move( hotkeys ) } };
}

std::optional<InputMap> InputMap::fromJson( control::Json const& json )
{
  if ( !json.is_object() || !json.contains( "players" ) || !json.at( "players" ).is_array() )
  {
    return std::nullopt;
  }
  InputMap map;
  auto const& players = json.at( "players" );
  for ( std::size_t p = 0; p < PLAYERS && p < players.size(); ++p )
  {
    control::Json const& entry = players.at( p );
    Player& player = map.mPlayers.at( p );
    if ( entry.contains( "gamepad" ) && entry.at( "gamepad" ).is_number_integer() )
    {
      player.gamepad = entry.at( "gamepad" ).get<int>();
    }
    if ( !entry.contains( "bindings" ) || !entry.at( "bindings" ).is_object() )
    {
      continue;
    }
    for ( std::size_t c = 0; c < CONTROLS; ++c )
    {
      std::string const name{ NAMES.at( c ).key };
      if ( entry.at( "bindings" ).contains( name ) )
      {
        player.bindings.at( c ) = decodeAll( entry.at( "bindings" ).at( name ) );
      }
    }
  }
  // A map saved before there were hotkeys gains them.
  map.mHotkeys = defaultHotkeys();
  control::Json const hotkeys = json.value( "hotkeys", control::Json::object() );
  for ( std::size_t h = 0; h < HOTKEYS; ++h )
  {
    std::string const name{ HOTKEY_NAMES.at( h ).key };
    if ( hotkeys.is_object() && hotkeys.contains( name ) )
    {
      map.mHotkeys.at( h ) = decodeAll( hotkeys.at( name ) );
    }
  }
  return map;
}

InputMap InputMap::load( std::filesystem::path const& path )
{
  std::ifstream stream{ path };
  if ( !stream )
  {
    return defaults();
  }
  auto const json = control::Json::parse( stream, nullptr, false );
  auto map = fromJson( json );
  if ( !map )
  {
    spdlog::warn( "{} is not an input map; the default one is used", path.string() );
    return defaults();
  }
  return std::move( *map );
}

void InputMap::save( std::filesystem::path const& path ) const
{
  std::ofstream stream{ path };
  stream << toJson().dump( 2 ) << '\n';
  if ( !stream )
  {
    spdlog::warn( "cannot write the input map to {}", path.string() );
  }
}

} // namespace pgm::app
