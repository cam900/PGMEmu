add_executable( pgm_cli src/Main.cpp )

set_target_properties( pgm_cli PROPERTIES OUTPUT_NAME pgmemu-cli )

target_link_libraries( pgm_cli
  PRIVATE pgm::server CLI11::CLI11 pgm::warnings )
