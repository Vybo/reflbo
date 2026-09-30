# Wi-Fi manager (spec §10.1, §10.2): saved networks, the AP with captive DNS, mDNS. netmgr_dns.c,
# netmgr_link.c, netmgr_list.c and netmgr_scan.c are pure C and also built on the host.
idf_component_register(SRCS "netmgr.c" "netmgr_dns.c" "netmgr_link.c" "netmgr_list.c" "netmgr_scan.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_event esp_netif esp_wifi lwip nvs_flash)
