cmake_minimum_required(VERSION 3.16)
project(faircmd LANGUAGES CXX)

# Header-only library that exports the include/ path
add_library(faircmd INTERFACE)
target_include_directories(faircmd INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(faircmd INTERFACE cxx_std_17)

# machine demo (FAIRCMD_MODE=2)
add_executable(machine_basic examples/machine_basic.cpp)
target_link_libraries(machine_basic PRIVATE faircmd)
target_compile_definitions(machine_basic PRIVATE FAIRCMD_MODE=2)

# hybrid interactive (FAIRCMD_MODE=0)
add_executable(hybrid_interactive examples/hybrid_interactive.cpp)
target_link_libraries(hybrid_interactive PRIVATE faircmd)
target_compile_definitions(hybrid_interactive PRIVATE FAIRCMD_MODE=0)

# split interactive (FAIRCMD_MODE=1) — uses its own source
add_executable(split_interactive examples/split_interactive.cpp)
target_link_libraries(split_interactive PRIVATE faircmd)
target_compile_definitions(split_interactive PRIVATE FAIRCMD_MODE=1)
