cmake_minimum_required(VERSION 3.16)
project(faircmd LANGUAGES CXX)
add_library(faircmd INTERFACE)
target_include_directories(faircmd INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(faircmd INTERFACE cxx_std_17)

add_executable(machine_basic examples/machine_basic.cpp)
target_link_libraries(machine_basic PRIVATE faircmd)
target_compile_definitions(machine_basic PRIVATE FAIRCMD_MODE=2)

add_executable(hybrid_interactive examples/hybrid_interactive.cpp)
target_link_libraries(hybrid_interactive PRIVATE faircmd)
target_compile_definitions(hybrid_interactive PRIVATE FAIRCMD_MODE=0)
