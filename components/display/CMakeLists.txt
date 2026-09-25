# Display service: canonical framebuffer + panel pushes (spec §4.1).
idf_component_register(SRCS "display.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx st7305
                       PRIV_REQUIRES esp_timer util)
