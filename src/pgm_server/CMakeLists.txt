add_library( pgm_server
  src/JsonLinesServer.cpp )

add_library( pgm::server ALIAS pgm_server )

target_include_directories( pgm_server PUBLIC include )

target_link_libraries( pgm_server
  PUBLIC  pgm::core
  PRIVATE pgm::warnings )
