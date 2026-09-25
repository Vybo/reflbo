cmake_minimum_required(VERSION 3.16)

# Personal overrides (e.g. dev Wi-Fi) go in the gitignored sdkconfig.defaults.local.
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.local")
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.defaults.local")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(reflbo)
