add_executable( pgm_tests
  RomSetTest.cpp
  VersionTest.cpp
  cart/BiosTest.cpp
  cart/PgmImageTest.cpp
  control/DispatcherTest.cpp
  control/LoadAndMemoryTest.cpp
  cpu/M68kSingleStepTest.cpp
  io/RomSourcesTest.cpp
  machine/Igs023Test.cpp
  machine/Igs026Test.cpp
  machine/MachineTest.cpp
  machine/V3021Test.cpp
  server/JsonLinesServerTest.cpp
  support/Files.cpp
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
          PGM_TEST_680X0_DIR="${CMAKE_SOURCE_DIR}/build/tools/680x0" )

# miniz's and Moira's headers only: the suite writes zips with one and runs the
# 68000 suite on the other, and their code arrives with pgm_core, which links
# both. Linking them here as well puts each archive on the link line twice,
# which Apple's linker warns about.
target_include_directories( pgm_tests SYSTEM PRIVATE
  $<TARGET_PROPERTY:miniz,INTERFACE_INCLUDE_DIRECTORIES>
  $<TARGET_PROPERTY:moira,INTERFACE_INCLUDE_DIRECTORIES> )

target_link_libraries( pgm_tests
  PRIVATE pgm::server Catch2::Catch2WithMain pgm::warnings )

include( Catch )
catch_discover_tests( pgm_tests )
