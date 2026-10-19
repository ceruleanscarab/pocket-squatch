# The watch app's native library: the same sim and scene as everywhere else, plus a JNI bridge.
cmake_minimum_required(VERSION 3.16)
project(pocket_squatch_wear CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_subdirectory(${REPO}/sim sim)
add_subdirectory(${REPO}/scene scene)

add_library(squatchwear SHARED src/main/cpp/wear_jni.cpp)
target_link_libraries(squatchwear PRIVATE squatch_scene)
