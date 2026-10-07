# floooh/chips' Z80 and its disassembler, vendored. See ../README.md for
# provenance and the update rule.
#
# Both are single-header libraries: the implementation is compiled into the one
# translation unit of ours that defines CHIPS_IMPL, so this is an interface
# target carrying the include directory, SYSTEM for the reason ../Moira gives.
add_library( chips_z80 INTERFACE )

target_include_directories( chips_z80 SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}" )
