# 1-bpp drawing, fonts and icons (spec §4.3–4.5). Pure C: also built on the host by test/host.
# Sources are listed, not globbed: ESP-IDF globs SRC_DIRS only at configure time.
idf_component_register(SRCS "gfx.c" "gfx_pbm.c" "gfx_test_pattern.c" "gfx_text.c"
                            "fonts/gfx_font_sans_12.c" "fonts/gfx_font_sans_16.c"
                            "fonts/gfx_font_sans_20.c" "fonts/gfx_font_sans_bold_20.c"
                            "fonts/gfx_font_sans_bold_16.c" "fonts/gfx_font_sans_bold_28.c"
                            "fonts/gfx_font_num_cb_48.c" "fonts/gfx_font_num_cb_72.c" "fonts/gfx_font_num_cb_110.c"
                            "fonts/gfx_font_num_cb_130.c"
                            "icons/gfx_icons.c"
                       INCLUDE_DIRS "include")
