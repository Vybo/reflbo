# Web configurator (spec §10.3, §10.4). The files here are pure C and also built on the host; Task 9
# of the M4 plan adds the server itself.
idf_component_register(SRCS "webui_auth.c" "webui_http.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES util)
