# The clearing scene: renderer, gestures and the pet's screen. No hardware code, so it
# builds for the PC (shown in an SDL window) and for the ESP32-S3 (drawn to the panel).
if(ESP_PLATFORM)
  idf_component_register(SRCS src/canvas.cpp src/gesture.cpp src/scene.cpp src/sounds.cpp INCLUDE_DIRS include REQUIRES sim)
else()
  add_library(squatch_scene STATIC src/canvas.cpp src/gesture.cpp src/scene.cpp src/sounds.cpp)
  target_include_directories(squatch_scene PUBLIC include)
  target_link_libraries(squatch_scene PUBLIC squatch_sim)
endif()
