cmake_minimum_required(VERSION 3.16)
project(reflbo_host_tests C)

# Host-side tests for the IDF-free components (spec §17). From the repo root:
#   cmake -S test/host -B build-host -G Ninja && cmake --build build-host \
#     && ctest --test-dir build-host --output-on-failure

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(REFLBO_WARNINGS -Wall -Wextra -Werror)

enable_testing()

add_library(unity STATIC third_party/unity/unity.c)
target_include_directories(unity PUBLIC third_party/unity)

# Components under test: the same sources the firmware compiles.
file(GLOB UTIL_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/components/util/*.c)
add_library(util STATIC ${UTIL_SOURCES})
target_include_directories(util PUBLIC ${REPO_ROOT}/components/util/include)
target_compile_options(util PRIVATE ${REFLBO_WARNINGS})

# gfx: every source in the component, generated fonts included.
file(GLOB GFX_SOURCES CONFIGURE_DEPENDS
     ${REPO_ROOT}/components/gfx/*.c ${REPO_ROOT}/components/gfx/fonts/*.c)
add_library(gfx STATIC ${GFX_SOURCES})
target_include_directories(gfx PUBLIC ${REPO_ROOT}/components/gfx/include)
target_compile_options(gfx PRIVATE ${REFLBO_WARNINGS})

# st7305: only the pure-C frame conversion builds on the host.
add_library(st7305_frame STATIC ${REPO_ROOT}/components/st7305/st7305_frame.c)
target_include_directories(st7305_frame PUBLIC ${REPO_ROOT}/components/st7305/include)
target_compile_options(st7305_frame PRIVATE ${REFLBO_WARNINGS})

# reflbo_host_test(<name> <libs...>): builds <name>.c against Unity and registers it with ctest.
function(reflbo_host_test name)
    add_executable(${name} ${name}.c)
    target_compile_options(${name} PRIVATE ${REFLBO_WARNINGS})
    target_link_libraries(${name} PRIVATE unity ${ARGN})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

reflbo_host_test(test_util_crc32 util)
reflbo_host_test(test_util_base64 util)
reflbo_host_test(test_util_ticks util)
reflbo_host_test(test_gfx gfx)
reflbo_host_test(test_gfx_text gfx)
reflbo_host_test(test_gfx_fonts gfx)
reflbo_host_test(test_st7305_frame st7305_frame)
reflbo_host_test(test_util_snapshot util)
reflbo_host_test(test_util_time util)

reflbo_host_test(test_test_pattern_golden gfx)
target_compile_definitions(test_test_pattern_golden PRIVATE GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

add_executable(render_test_pattern render_test_pattern.c)
target_compile_options(render_test_pattern PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(render_test_pattern PRIVATE gfx)

# Python tool tests (stdlib unittest; pyserial is not needed).
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME tools_unittests
         COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests -t . -v
         WORKING_DIRECTORY ${REPO_ROOT}/tools)
