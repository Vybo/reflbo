# The web configurator (spec §10.3, §10.4). webui_auth.c and webui_http.c are pure C and also built
# on the host. The pages in web/ are gzipped at build time and embedded in the app image, so an OTA
# update brings them along (spec §5.3).
idf_component_register(SRCS "webui.c" "webui_auth.c" "webui_http.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES app_update bootloader_support esp_app_format esp_http_server esp_timer json weather
                                     lwip netmgr nvs_flash util)

set(web_dir "${CMAKE_CURRENT_LIST_DIR}/../../web")
foreach(page index.html app.js style.css zones.js)
    set(gz "${CMAKE_CURRENT_BINARY_DIR}/${page}.gz")
    # -n leaves out the name and time, so the same page gives the same image
    add_custom_command(OUTPUT "${gz}"
                       COMMAND gzip -9 -n -c "${web_dir}/${page}" > "${gz}"
                       DEPENDS "${web_dir}/${page}"
                       COMMENT "Compressing web/${page}")
    target_add_binary_data(${COMPONENT_LIB} "${gz}" BINARY)
endforeach()
