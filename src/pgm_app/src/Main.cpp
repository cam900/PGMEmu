// The desktop entry point: everything it does is Application's.

#include "Application.hpp"

#include <SDL3/SDL_main.h>
#include <spdlog/spdlog.h>

#include <cstdio>
#include <exception>

namespace
{

int run()
{
  auto application = pgm::app::Application::create();
  if ( !application )
  {
    spdlog::critical( "cannot start: {}", application.error() );
    return 1;
  }
  ( *application )->run();
  return 0;
}

} // namespace

// Nothing may escape main, for the reasons pgmemu-cli's main gives.
int main( int /*argc*/, char** /*argv*/ )
{
  try
  {
    return run();
  }
  catch ( std::exception const& error )
  {
    std::fprintf( stderr, "pgmemu: %s\n", error.what() );
    return 2;
  }
  catch ( ... )
  {
    std::fputs( "pgmemu: internal error: unknown exception\n", stderr );
    return 2;
  }
}
