# The weather radar (spec §11.2): ČHMÚ's composite and RainViewer's tiles decoded into rain levels,
# their store, and their drawing into any map view. These sources are pure C and also build on the
# host; radar_fetch.c fetches the images on the sync task.
idf_component_register(SRCS "radar_frame.c" "radar_chmu.c" "radar_rainviewer.c" "radar_store.c" "radar_fetch.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx map png esp_common
                       PRIV_REQUIRES json util fetch esp_timer heap log)
