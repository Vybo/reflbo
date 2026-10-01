# The map under both radars (spec §11.1): web-Mercator views, the built-in borders, coasts, towns
# and airports, and their drawing. Pure C: also built on the host. tools/gen_map.sh packs
# assets/map/map.bin, embedded here as _binary_map_bin_start.._end.
idf_component_register(SRCS "map_view.c" "map_data.c" "map_draw.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx
                       PRIV_REQUIRES util)

target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_LIST_DIR}/../../assets/map/map.bin" BINARY)
