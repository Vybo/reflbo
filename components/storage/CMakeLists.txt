# LittleFS config files and the settings codec (spec §14). settings.c and storage_file.c are pure
# C (cJSON and POSIX files) and are also built on the host by test/host.
idf_component_register(SRCS "storage.c" "storage_file.c" "settings.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES json util vfs joltwallet__littlefs)
