#include <catch2/catch_test_macros.hpp>

#include "pgm/cart/Orientation.hpp"

using pgm::cart::Orientation;
using pgm::cart::orientationOf;

TEST_CASE( "CAVE's shooters are vertical and the rest horizontal", "[cart]" )
{
  for ( auto const* name : { "ddp2", "ddp3", "ddpdoj", "ket", "ketarr", "espgal" } )
  {
    CAPTURE( name );
    CHECK( orientationOf( name ) == Orientation::VERTICAL );
  }
  for ( auto const* name : { "orlegend", "kov2", "dmnfrnt", "martmast", "pgm", "" } )
  {
    CAPTURE( name );
    CHECK( orientationOf( name ) == Orientation::HORIZONTAL );
  }
}
