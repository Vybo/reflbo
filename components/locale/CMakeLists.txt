# Language packs (spec §5.8). Pure C: also built on the host by test/host.
idf_component_register(SRCS "lang.c" "lang_en.c"
                       INCLUDE_DIRS "include")
