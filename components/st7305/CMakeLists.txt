# ST7305 reflective LCD (spec §4.2). st7305_frame.c is pure C and also built on the host.
idf_component_register(SRCS "st7305_frame.c"
                       INCLUDE_DIRS "include")
