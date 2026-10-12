# BananaDS 2D engine: the engine as a static library plus the differential test harness, built against
# a melonDS source tree (fetch-melonds.sh gets the pinned commit and applies melonds-core.patch).
#
#   cmake -S . -B build -G Ninja -DMELONDS_SRC=third_party/melonDS && cmake --build build
#   ./build/banana2d-test
cmake_minimum_required(VERSION 3.16)
project(bananads2d LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(MELONDS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/melonDS" CACHE PATH "melonDS source tree with melonds-core.patch applied")
if (NOT EXISTS "${MELONDS_SRC}/src/GPU_Soft.h")
    message(FATAL_ERROR "no melonDS tree at ${MELONDS_SRC}: run fetch-melonds.sh or pass -DMELONDS_SRC=...")
endif()

# a headless core: no frontend, no OpenGL, no GDB stub, no JIT (the harness never runs the CPUs), no LTO
set(BUILD_QT_SDL OFF CACHE BOOL "" FORCE)
set(ENABLE_OGLRENDERER OFF CACHE BOOL "" FORCE)
set(ENABLE_GDBSTUB OFF CACHE BOOL "" FORCE)
set(ENABLE_JIT OFF CACHE BOOL "" FORCE)
set(ENABLE_LTO_RELEASE OFF CACHE BOOL "" FORCE)
set(ENABLE_LTO OFF CACHE BOOL "" FORCE)
add_subdirectory("${MELONDS_SRC}" melonds EXCLUDE_FROM_ALL)

add_library(banana2d STATIC gpu2d/GPU2D_Banana.cpp gpu2d/GPU_Banana.cpp)
target_include_directories(banana2d PUBLIC gpu2d)
target_link_libraries(banana2d PUBLIC core)
target_compile_options(banana2d PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare)
if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    # every x86-64 CPU since 2008: blendv for the compositor's selects
    target_compile_options(banana2d PRIVATE -msse4.1)
endif()

find_package(Threads REQUIRED)
add_executable(banana2d-test tests/harness.cpp tests/platform_stub.cpp)
target_link_libraries(banana2d-test PRIVATE banana2d core Threads::Threads ${CMAKE_DL_LIBS})
