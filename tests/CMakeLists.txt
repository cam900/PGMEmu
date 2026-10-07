add_executable( pgm_tests
  RomSetTest.cpp
  VersionTest.cpp
  cart/BiosTest.cpp
  cart/PgmImageTest.cpp
  control/DispatcherTest.cpp
  control/LoadAndMemoryTest.cpp
  io/RomSourcesTest.cpp
  server/JsonLinesServerTest.cpp
  support/Files.cpp
  support/PgmFile.cpp )

target_include_directories( pgm_tests PRIVATE . )

# Where RomSetTest finds the real sets and the images built from them. Neither
# is in the tree; the tests that need them skip when they are absent.
target_compile_definitions( pgm_tests
  PRIVATE PGM_TEST_PGM_DIR="${CMAKE_SOURCE_DIR}/roms"
          PGM_TEST_ROMS_DIR="${CMAKE_SOURCE_DIR}/../ROMS" )

# miniz's headers only: the suite writes zips with it, and its code arrives with
# pgm_core, which links it. Linking it here as well puts the archive on the link
# line twice, which Apple's linker warns about.
target_include_directories( pgm_tests SYSTEM PRIVATE $<TARGET_PROPERTY:miniz,INTERFACE_INCLUDE_DIRECTORIES> )

target_link_libraries( pgm_tests
  PRIVATE pgm::server Catch2::Catch2WithMain pgm::warnings )

include( Catch )
catch_discover_tests( pgm_tests )
