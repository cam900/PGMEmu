add_executable( pgm_app
  src/Application.cpp
  src/Keyboard.cpp
  src/Main.cpp
  src/ScreenTexture.cpp
  src/TestPattern.cpp )

set_target_properties( pgm_app PROPERTIES OUTPUT_NAME pgmemu )

target_link_libraries( pgm_app
  PRIVATE pgm::core imgui SDL3::SDL3-static CLI11::CLI11 pgm::warnings )
