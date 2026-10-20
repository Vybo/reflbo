# The PC app (spec §12a, D44): its messages and the session's rules (pc_msg.c, pc_session.c, also built on the host).
idf_component_register(SRCS "pc_msg.c" "pc_session.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES json util)
