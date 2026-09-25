# 1-bpp drawing and fonts (spec §4.3–4.4). Pure C: also built on the host by test/host.
# Sources are listed, not globbed: ESP-IDF globs SRC_DIRS only at configure time.
idf_component_register(SRCS "gfx.c" "gfx_pbm.c" "gfx_text.c"
                       INCLUDE_DIRS "include")
