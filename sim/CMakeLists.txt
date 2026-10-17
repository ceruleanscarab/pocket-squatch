# Simulation core: no hardware dependencies, builds for the PC and the ESP32-S3.
if(ESP_PLATFORM)
  idf_component_register(SRCS src/sim.cpp src/save.cpp src/dna.cpp src/trails.cpp INCLUDE_DIRS include)
else()
  add_library(squatch_sim STATIC src/sim.cpp src/save.cpp src/dna.cpp src/trails.cpp)
  target_include_directories(squatch_sim PUBLIC include)
endif()
