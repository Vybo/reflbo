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

# cmake -DREFLBO_SANITIZE=ON: everything with AddressSanitizer and UndefinedBehaviorSanitizer.
option(REFLBO_SANITIZE "Build the host tests with ASan and UBSan" OFF)
if(REFLBO_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
endif()

enable_testing()

add_library(unity STATIC third_party/unity/unity.c)
target_include_directories(unity PUBLIC third_party/unity)

# Components under test: the same sources the firmware compiles.
file(GLOB UTIL_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/components/util/*.c)
add_library(util STATIC ${UTIL_SOURCES})
target_link_libraries(util PUBLIC m)
target_include_directories(util PUBLIC ${REPO_ROOT}/components/util/include)
target_compile_options(util PRIVATE ${REFLBO_WARNINGS})

# gfx: every source in the component, generated fonts and icons included.
file(GLOB GFX_SOURCES CONFIGURE_DEPENDS
     ${REPO_ROOT}/components/gfx/*.c ${REPO_ROOT}/components/gfx/fonts/*.c ${REPO_ROOT}/components/gfx/icons/*.c
     ${REPO_ROOT}/components/gfx/qrcodegen/*.c)
add_library(gfx STATIC ${GFX_SOURCES})
target_include_directories(gfx PUBLIC ${REPO_ROOT}/components/gfx/include
                           PRIVATE ${REPO_ROOT}/components/gfx/qrcodegen)
target_compile_options(gfx PRIVATE ${REFLBO_WARNINGS})

# st7305: only the pure-C frame conversion builds on the host.
add_library(st7305_frame STATIC ${REPO_ROOT}/components/st7305/st7305_frame.c)
target_include_directories(st7305_frame PUBLIC ${REPO_ROOT}/components/st7305/include)
target_compile_options(st7305_frame PRIVATE ${REFLBO_WARNINGS})

# scheduler: pure C.
add_library(scheduler STATIC ${REPO_ROOT}/components/scheduler/scheduler.c)
target_include_directories(scheduler PUBLIC ${REPO_ROOT}/components/scheduler/include)
target_compile_options(scheduler PRIVATE ${REFLBO_WARNINGS})

# astro: pure C.
add_library(astro STATIC ${REPO_ROOT}/components/astro/astro.c)
target_include_directories(astro PUBLIC ${REPO_ROOT}/components/astro/include)
target_compile_options(astro PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(astro PUBLIC m)

# sync: the planner (modes, quiet hours, retries) and the SNTP packets build on the host; the task does not.
add_library(sync_logic STATIC ${REPO_ROOT}/components/sync/sync_plan.c ${REPO_ROOT}/components/sync/sync_ntp.c)
target_include_directories(sync_logic PUBLIC ${REPO_ROOT}/components/sync/include)
target_compile_options(sync_logic PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(sync_logic PUBLIC scheduler)

# board: only the pure gesture recogniser builds on the host.
add_library(board_logic STATIC ${REPO_ROOT}/components/board/gesture.c)
target_include_directories(board_logic PUBLIC ${REPO_ROOT}/components/board/include)
target_compile_options(board_logic PRIVATE ${REFLBO_WARNINGS})

# sensors: only the SHTC3 codec and the battery model build on the host.
add_library(sensors_logic STATIC ${REPO_ROOT}/components/sensors/shtc3_codec.c
                                 ${REPO_ROOT}/components/sensors/battery_model.c
                                 ${REPO_ROOT}/components/sensors/battery_learn.c)
target_include_directories(sensors_logic PUBLIC ${REPO_ROOT}/components/sensors/include)
target_link_libraries(sensors_logic PUBLIC util)
target_compile_options(sensors_logic PRIVATE ${REFLBO_WARNINGS})

# rtc: only the PCF85063 register codec builds on the host.
add_library(rtc_logic STATIC ${REPO_ROOT}/components/rtc/pcf85063_regs.c)
target_include_directories(rtc_logic PUBLIC ${REPO_ROOT}/components/rtc/include)
target_compile_options(rtc_logic PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(rtc_logic PUBLIC util)

# timekeeping: the ISO 8601 parser, the zone list, the RTC sync rule and the trim build on the host.
add_library(timekeeping_logic STATIC ${REPO_ROOT}/components/timekeeping/timekeeping_iso.c
            ${REPO_ROOT}/components/timekeeping/timekeeping_zones.c
            ${REPO_ROOT}/components/timekeeping/timekeeping_sync.c
            ${REPO_ROOT}/components/timekeeping/timekeeping_trim.c)
target_include_directories(timekeeping_logic PUBLIC ${REPO_ROOT}/components/timekeeping/include)
target_compile_options(timekeeping_logic PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(timekeeping_logic PUBLIC util)

# netmgr: the captive DNS reply, the saved-network list, the scan choices and the link events build
# on the host.
add_library(netmgr_logic STATIC ${REPO_ROOT}/components/netmgr/netmgr_dns.c
            ${REPO_ROOT}/components/netmgr/netmgr_list.c ${REPO_ROOT}/components/netmgr/netmgr_scan.c
            ${REPO_ROOT}/components/netmgr/netmgr_link.c)
target_include_directories(netmgr_logic PUBLIC ${REPO_ROOT}/components/netmgr/include)
target_compile_options(netmgr_logic PRIVATE ${REFLBO_WARNINGS})

# webui: the password record, the sessions and the HTTP helpers build on the host.
add_library(webui_logic STATIC ${REPO_ROOT}/components/webui/webui_auth.c
            ${REPO_ROOT}/components/webui/webui_http.c)
target_include_directories(webui_logic PUBLIC ${REPO_ROOT}/components/webui/include)
target_compile_options(webui_logic PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(webui_logic PUBLIC util)

# power: only the sleep policy builds on the host.
add_library(power_logic STATIC ${REPO_ROOT}/components/power/power_policy.c)
target_include_directories(power_logic PUBLIC ${REPO_ROOT}/components/power/include)
target_compile_options(power_logic PRIVATE ${REFLBO_WARNINGS})

# locale: pure C language packs.
add_library(locale STATIC ${REPO_ROOT}/components/locale/lang.c ${REPO_ROOT}/components/locale/lang_en.c
            ${REPO_ROOT}/components/locale/lang_cs.c)
target_include_directories(locale PUBLIC ${REPO_ROOT}/components/locale/include)
target_compile_options(locale PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(locale PRIVATE util)

# datastore: pure C field store.
add_library(datastore STATIC ${REPO_ROOT}/components/datastore/datastore.c)
target_include_directories(datastore PUBLIC ${REPO_ROOT}/components/datastore/include)
target_compile_options(datastore PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(datastore PUBLIC m)

# cJSON: the copy bundled with ESP-IDF, so the host tests parse exactly like the firmware.
set(REFLBO_IDF_PATH "$ENV{REFLBO_IDF_PATH}")
if(NOT REFLBO_IDF_PATH)
    set(REFLBO_IDF_PATH "$ENV{HOME}/esp/esp-idf-v5.5.5")
endif()
add_library(cjson STATIC ${REFLBO_IDF_PATH}/components/json/cJSON/cJSON.c)
target_include_directories(cjson PUBLIC ${REFLBO_IDF_PATH}/components/json/cJSON)
target_compile_options(cjson PRIVATE -Wno-deprecated-declarations) # its sprintf calls; third-party code
target_link_libraries(cjson PUBLIC m)

# storage: the settings codec and the config-file logic build on the host; the mount does not.
add_library(storage_logic STATIC
            ${REPO_ROOT}/components/storage/settings.c ${REPO_ROOT}/components/storage/storage_file.c
            ${REPO_ROOT}/components/storage/storage_backup.c)
target_include_directories(storage_logic PUBLIC ${REPO_ROOT}/components/storage/include)
target_compile_options(storage_logic PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(storage_logic PRIVATE cjson util m)

# weather: the Open-Meteo requests, replies, bands and levels build on the host; the fetch does not.
add_library(weather_logic STATIC ${REPO_ROOT}/components/weather/weather_url.c
            ${REPO_ROOT}/components/weather/weather_parse.c ${REPO_ROOT}/components/weather/weather_levels.c)
target_include_directories(weather_logic PUBLIC ${REPO_ROOT}/components/weather/include)
target_compile_options(weather_logic PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(weather_logic PUBLIC datastore PRIVATE cjson util m)

# ui: pure C on top of gfx, locale, the datastore and the scheduler's local-time rules.
file(GLOB UI_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/components/ui/*.c)
add_library(ui STATIC ${UI_SOURCES})
target_include_directories(ui PUBLIC ${REPO_ROOT}/components/ui/include)
target_compile_options(ui PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(ui PUBLIC gfx locale datastore util PRIVATE cjson scheduler weather_logic astro m)

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
reflbo_host_test(test_gfx_bmp_qr gfx)
reflbo_host_test(test_st7305_frame st7305_frame)
reflbo_host_test(test_util_snapshot util)
reflbo_host_test(test_util_time util)
reflbo_host_test(test_util_calendar util)
reflbo_host_test(test_util_json util)
reflbo_host_test(test_util_sha256 util)
reflbo_host_test(test_scheduler scheduler)
reflbo_host_test(test_astro astro)
reflbo_host_test(test_sync_plan sync_logic)
reflbo_host_test(test_gesture board_logic)
reflbo_host_test(test_shtc3_codec sensors_logic)
reflbo_host_test(test_battery_model sensors_logic)
reflbo_host_test(test_battery_learn sensors_logic)
reflbo_host_test(test_pcf85063_regs rtc_logic)
reflbo_host_test(test_timekeeping_iso timekeeping_logic)
reflbo_host_test(test_timekeeping_zones timekeeping_logic)
reflbo_host_test(test_timekeeping_sync timekeeping_logic)
reflbo_host_test(test_timekeeping_trim timekeeping_logic)
reflbo_host_test(test_power_policy power_logic)
reflbo_host_test(test_netmgr_dns netmgr_logic)
reflbo_host_test(test_netmgr_list netmgr_logic)
reflbo_host_test(test_netmgr_scan netmgr_logic)
reflbo_host_test(test_netmgr_link netmgr_logic)
reflbo_host_test(test_webui_auth webui_logic)
reflbo_host_test(test_webui_http webui_logic)
reflbo_host_test(test_lang locale)
reflbo_host_test(test_lang_glyphs locale gfx)
reflbo_host_test(test_datastore datastore)
reflbo_host_test(test_datastore_weather datastore)
reflbo_host_test(test_weather weather_logic)
reflbo_host_test(test_sync_ntp sync_logic)
target_compile_definitions(test_weather PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/open-meteo")
reflbo_host_test(test_ui_fields ui)
reflbo_host_test(test_ui_preset ui)
reflbo_host_test(test_ui_schedule ui)
reflbo_host_test(test_ui_widget_fit ui)
reflbo_host_test(test_ui_menu ui)
reflbo_host_test(test_ui_config ui)
reflbo_host_test(test_ui_catalog ui cjson)
reflbo_host_test(test_settings storage_logic)
reflbo_host_test(test_storage_file storage_logic)
target_compile_definitions(test_storage_file PRIVATE TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/storage_tmp")
reflbo_host_test(test_storage_backup storage_logic)

reflbo_host_test(test_test_pattern_golden gfx)
target_compile_definitions(test_test_pattern_golden PRIVATE GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

add_executable(render_test_pattern render_test_pattern.c)
target_compile_options(render_test_pattern PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(render_test_pattern PRIVATE gfx)

reflbo_host_test(test_ui_dashboard_golden ui)
target_compile_definitions(test_ui_dashboard_golden PRIVATE GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

add_executable(render_dashboard render_dashboard.c)
target_compile_options(render_dashboard PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(render_dashboard PRIVATE ui)

reflbo_host_test(test_ui_screens_golden ui timekeeping_logic)
target_compile_definitions(test_ui_screens_golden PRIVATE GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

add_executable(render_screen render_screen.c)
target_compile_options(render_screen PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(render_screen PRIVATE ui timekeeping_logic)

# Python tool tests (stdlib unittest; pyserial is not needed).
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
add_test(NAME tools_unittests
         COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests -t . -v
         WORKING_DIRECTORY ${REPO_ROOT}/tools)
set_tests_properties(tools_unittests PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1") # no tools/__pycache__

# The web page's script against a fake device (Node's own test runner; no packages).
find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE)
    add_test(NAME web_unittests COMMAND ${NODE_EXECUTABLE} --test ${REPO_ROOT}/test/web/test_app.mjs)
else()
    message(STATUS "node not found: the web page tests are left out")
endif()
