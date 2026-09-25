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
add_library(util STATIC ${REPO_ROOT}/components/util/util_crc32.c)
target_include_directories(util PUBLIC ${REPO_ROOT}/components/util/include)
target_compile_options(util PRIVATE ${REFLBO_WARNINGS})

# reflbo_host_test(<name> <libs...>): builds <name>.c against Unity and registers it with ctest.
function(reflbo_host_test name)
    add_executable(${name} ${name}.c)
    target_compile_options(${name} PRIVATE ${REFLBO_WARNINGS})
    target_link_libraries(${name} PRIVATE unity ${ARGN})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

reflbo_host_test(test_util_crc32 util)
