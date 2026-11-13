cmake_minimum_required(VERSION 3.20)
project(astar_visualizer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ASTAR_CPP_DIR "/Users/justinhj/projects/astar-algorithm-cpp" CACHE PATH "Path to astar-algorithm-cpp repository")
if(NOT EXISTS "${ASTAR_CPP_DIR}/stlastar.h")
    message(FATAL_ERROR "Could not find stlastar.h in ASTAR_CPP_DIR=${ASTAR_CPP_DIR}")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3_DEP REQUIRED IMPORTED_TARGET sdl3)
pkg_check_modules(CAIRO_DEP REQUIRED IMPORTED_TARGET cairo)

add_library(astar_core STATIC
    src/graph.cpp
    src/astar_runner.cpp
)
target_include_directories(astar_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${ASTAR_CPP_DIR}
)
target_compile_options(astar_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(astar_visualizer
    src/main.cpp
    src/renderer.cpp
)
target_link_libraries(astar_visualizer PRIVATE
    astar_core
    PkgConfig::SDL3_DEP
    PkgConfig::CAIRO_DEP
)
target_compile_options(astar_visualizer PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_executable(astar_tests
    tests/test_astar_graph.cpp
)
target_link_libraries(astar_tests PRIVATE astar_core)
target_compile_options(astar_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME astar_tests COMMAND astar_tests)
