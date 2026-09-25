# Small pure-C helpers shared by the firmware and the host tests (no ESP-IDF headers).
idf_component_register(SRCS "util_crc32.c"
                       INCLUDE_DIRS "include")
