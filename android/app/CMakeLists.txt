# The game as libmain.so, plus SDL2 built from source (android/fetch-sdl.sh puts it in android/SDL).
cmake_minimum_required(VERSION 3.16)
project(pocket_squatch_android C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_subdirectory(${REPO}/android/SDL SDL)
add_subdirectory(${REPO}/sim sim)
add_subdirectory(${REPO}/scene scene)

add_library(main SHARED src/main/cpp/main.cpp)
target_link_libraries(main PRIVATE squatch_scene SDL2::SDL2)
