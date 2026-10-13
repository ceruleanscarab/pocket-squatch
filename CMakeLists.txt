cmake_minimum_required(VERSION 3.16)
project(pocket_squatch CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# The simulation core. On the device this same folder becomes an ESP-IDF component
# (see sim/CMakeLists.txt); here it is a plain static library.
add_subdirectory(sim)
add_subdirectory(scene)

add_executable(squatch-harness harness/main.cpp)
target_link_libraries(squatch-harness PRIVATE squatch_sim)

enable_testing()
add_executable(squatch-tests tests/tests.cpp)
target_include_directories(squatch-tests PRIVATE harness)
target_link_libraries(squatch-tests PRIVATE squatch_sim)
add_test(NAME sim COMMAND squatch-tests)

add_executable(squatch-scene-tests tests/scene_tests.cpp)
target_link_libraries(squatch-scene-tests PRIVATE squatch_scene)
add_test(NAME scene COMMAND squatch-scene-tests)

# The PC scene needs SDL2 (in WSL: sudo apt install libsdl2-dev). Everything else builds without it.
find_package(SDL2 CONFIG QUIET)
if(SDL2_FOUND)
  add_executable(squatch-pc pc/main.cpp)
  target_link_libraries(squatch-pc PRIVATE squatch_scene SDL2::SDL2)
  if(TARGET SDL2::SDL2main)
    target_link_libraries(squatch-pc PRIVATE SDL2::SDL2main)
  endif()
else()
  message(STATUS "SDL2 not found: skipping squatch-pc")
endif()
