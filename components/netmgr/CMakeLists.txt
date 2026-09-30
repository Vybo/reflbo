# Wi-Fi manager (spec §10.1, §10.2): saved networks, the AP with captive DNS, mDNS. The files here
# are pure C and also built on the host; Task 8 of the M4 plan adds the manager itself.
idf_component_register(SRCS "netmgr_dns.c" "netmgr_list.c" "netmgr_scan.c"
                       INCLUDE_DIRS "include")
