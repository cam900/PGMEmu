add_library( pgm_core
  src/PortabilityChecks.cpp
  src/Version.cpp
  src/control/Dispatcher.cpp
  src/cpu/MoiraConfigCheck.cpp )

add_library( pgm::core ALIAS pgm_core )

target_include_directories( pgm_core PUBLIC include )

target_compile_definitions( pgm_core PRIVATE PGM_VERSION="${PGM_VERSION_STRING}" )

# nlohmann_json is PUBLIC because the dispatcher's interface is JSON: a request
# goes in as one and a response comes out as one, whichever transport carries
# them. The CPU cores are PRIVATE; nothing outside the core sees them.
target_link_libraries( pgm_core
  PUBLIC  nlohmann_json::nlohmann_json
  PRIVATE pgm::warnings moira chips_z80 )
