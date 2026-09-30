#include "netmgr_list.h"

#include <string.h>

bool netmgr_net_valid(const char *ssid, const char *pass)
{
    size_t s = ssid ? strlen(ssid) : 0, p = pass ? strlen(pass) : 0;
    if (s < 1 || s > NETMGR_SSID_MAX || p > NETMGR_PASS_MAX || (p > 0 && p < 8)) {
        return false;
    }
    for (size_t i = 0; i < p; i++) {
        char c = pass[i];
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (p == NETMGR_PASS_MAX ? !hex : (c < 0x20 || c > 0x7E)) {
            return false;
        }
    }
    return true;
}

int netmgr_list_find(const netmgr_list_t *l, const char *ssid)
{
    for (int i = 0; ssid != NULL && i < l->count; i++) {
        if (strcmp(l->nets[i].ssid, ssid) == 0) {
            return i;
        }
    }
    return -1;
}

/* Moves nets[index] to the front, keeping the others' order. */
static void to_front(netmgr_list_t *l, int index)
{
    netmgr_net_t net = l->nets[index];
    memmove(&l->nets[1], &l->nets[0], (size_t)index * sizeof(net));
    l->nets[0] = net;
}

bool netmgr_list_add(netmgr_list_t *l, const char *ssid, const char *pass)
{
    if (!netmgr_net_valid(ssid, pass)) {
        return false;
    }
    int index = netmgr_list_find(l, ssid);
    if (index < 0) {
        if (l->count < NETMGR_LIST_MAX) {
            l->count++;
        }
        index = l->count - 1; /* a full list gives up its last */
        memset(&l->nets[index], 0, sizeof(l->nets[index]));
        strcpy(l->nets[index].ssid, ssid);
    }
    netmgr_net_t *net = &l->nets[index];
    if (strcmp(net->pass, pass ? pass : "") != 0) { /* a new password may mean a new router */
        memset(net->bssid, 0, sizeof(net->bssid));
        net->channel = 0;
    }
    strcpy(net->pass, pass ? pass : "");
    to_front(l, index);
    return true;
}

void netmgr_list_succeeded(netmgr_list_t *l, int index, const uint8_t bssid[6], uint8_t channel)
{
    if (index < 0 || index >= l->count) {
        return;
    }
    memcpy(l->nets[index].bssid, bssid, 6);
    l->nets[index].channel = channel;
    to_front(l, index);
}

bool netmgr_list_forget(netmgr_list_t *l, const char *ssid)
{
    int index = netmgr_list_find(l, ssid);
    if (index < 0) {
        return false;
    }
    memmove(&l->nets[index], &l->nets[index + 1], (size_t)(l->count - index - 1) * sizeof(l->nets[0]));
    l->count--;
    memset(&l->nets[l->count], 0, sizeof(l->nets[0]));
    return true;
}
