# The PC app (spec §12a, D44): its messages (pc_msg.c, also built on the host).
idf_component_register(SRCS "pc_msg.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES json util)
