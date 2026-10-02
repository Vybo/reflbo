# A small PNG reader for the radar images (spec §11.2). png.c is pure C and also builds on the
# host, which inflates through zlib; png_inflate_rom.c uses the ROM's tinfl.
idf_component_register(SRCS "png.c" "png_inflate_rom.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES util esp_rom heap)
