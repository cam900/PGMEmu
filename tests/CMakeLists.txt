add_executable( pgm_tests
  VersionTest.cpp
  control/DispatcherTest.cpp
  server/JsonLinesServerTest.cpp )

target_include_directories( pgm_tests PRIVATE . )

target_link_libraries( pgm_tests
  PRIVATE pgm::server Catch2::Catch2WithMain pgm::warnings )

include( Catch )
catch_discover_tests( pgm_tests )
