add_executable( pgm_tests
  RomSetTest.cpp
  VersionTest.cpp
  cart/BiosTest.cpp
  cart/PgmImageTest.cpp
  control/AudioAndInputTest.cpp
  control/BatchTest.cpp
  control/DispatcherTest.cpp
  control/LoadAndMemoryTest.cpp
  control/TestMethodsTest.cpp
  control/VideoMethodsTest.cpp
  cpu/M68kSingleStepTest.cpp
  cpu/Z80SingleStepTest.cpp
  io/RomSourcesTest.cpp
  machine/Ics2115Test.cpp
  machine/Igs023DrawTest.cpp
  machine/Igs023Test.cpp
  machine/Igs026Test.cpp
  machine/MachineTest.cpp
  machine/SpriteEngineTest.cpp
  machine/V3021Test.cpp
  server/JsonLinesServerTest.cpp
  server/McpServerTest.cpp
  server/NetworkServersTest.cpp
  support/Files.cpp
  support/Fixture.cpp
  support/PgmFile.cpp
  support/Program.cpp )

# The core's private headers as well as its public ones: the devices of the
# machine are tested one by one, without the 68000 in front of them.
target_include_directories( pgm_tests PRIVATE . "${PROJECT_SOURCE_DIR}/src/pgm_core/src" )

# Where RomSetTest finds the real sets and the images built from them. Neither
# is in the tree; the tests that need them skip when they are absent.
target_compile_definitions( pgm_tests
  PRIVATE PGM_TEST_PGM_DIR="${CMAKE_SOURCE_DIR}/roms"
          PGM_TEST_ROMS_DIR="${CMAKE_SOURCE_DIR}/../ROMS"
          PGM_TEST_680X0_DIR="${CMAKE_SOURCE_DIR}/build/tools/680x0"
          PGM_TEST_Z80_DIR="${CMAKE_SOURCE_DIR}/build/tools/z80" )

# miniz's and Moira's headers only: the suite writes zips with one and runs the
# 68000 suite on the other, and their code arrives with pgm_core, which links
# both. Linking them here as well puts each archive on the link line twice,
# which Apple's linker warns about.
target_include_directories( pgm_tests SYSTEM PRIVATE
  $<TARGET_PROPERTY:miniz,INTERFACE_INCLUDE_DIRECTORIES>
  $<TARGET_PROPERTY:moira,INTERFACE_INCLUDE_DIRECTORIES> )

target_link_libraries( pgm_tests
  PRIVATE pgm::server Catch2::Catch2WithMain pgm::warnings httplib )

include( Catch )
catch_discover_tests( pgm_tests )

# The regression suite: every script in regression/ run by pgmemu-cli --batch,
# which checks it against the golden sums it holds (docs/spec/batch.md). A
# script whose game or BIOS program is absent exits 77 and is skipped, so is
# every one without the BIOS. New scripts need a reconfigure.
set( PGM_REGRESSION_BIOS "${CMAKE_SOURCE_DIR}/../ROMS/pgm.zip" )
set( PGM_REGRESSION_ROMS "${CMAKE_SOURCE_DIR}/roms" )
set( regression_sources )
if( EXISTS "${PGM_REGRESSION_BIOS}" )
  list( APPEND regression_sources --bios "${PGM_REGRESSION_BIOS}" )
endif()
if( IS_DIRECTORY "${PGM_REGRESSION_ROMS}" )
  list( APPEND regression_sources --rom-dir "${PGM_REGRESSION_ROMS}" )
endif()
file( GLOB regression_scripts CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/regression/*.json" )
foreach( script IN LISTS regression_scripts )
  get_filename_component( name "${script}" NAME_WE )
  add_test( NAME "regression.${name}"
            COMMAND pgm_cli --batch "${script}" ${regression_sources}
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" )
  set_tests_properties( "regression.${name}" PROPERTIES LABELS regression SKIP_RETURN_CODE 77 )
endforeach()

