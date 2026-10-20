# The PC app (spec §12a, D44): its messages and the session's rules (pc_msg.c, pc_session.c, also built on the host);
# the stream to it (pc_ws.c, device only).
idf_component_register(SRCS "pc_msg.c" "pc_session.c" "pc_ws.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES json util esp_websocket_client esp_timer)
