# Moira, vendored. See ../README.md for provenance and the update rule.
#
# Built without the project's warning set: upstream's warnings are its own
# business, and PGM_WERROR must not fail a build over a file this repository
# does not edit. The flags below are the ones upstream's own CMakeLists.txt
# passes, so that the library builds as its author builds it.
#
# MoiraConfig.h is not in this directory. It is ours, in src/pgm_core/moira,
# and is put on the include path here; see
# ../../docs/decisions/0009-moira-configuration.md.
add_library( moira STATIC
  Moira.cpp
  MoiraCore68000.cpp
  MoiraCore68010.cpp
  MoiraCore68020.cpp
  MoiraDebugger.cpp )

target_include_directories( moira SYSTEM PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${PROJECT_SOURCE_DIR}/src/pgm_core/moira" )

if( MSVC )
  target_compile_options( moira PRIVATE /bigobj /wd4100 /wd4201 /wd4324 /wd4458 /wd4127 )
else( )
  target_compile_options( moira PRIVATE
    -Wno-unused-parameter
    -Wno-unused-but-set-parameter
    -Wno-unused-but-set-variable
    -Wno-missing-field-initializers )
endif( )
