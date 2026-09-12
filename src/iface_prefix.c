#include "iface_prefix.h"

#include <ifaddrs.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>

int iface_prefix(const char *ifname) {
    struct ifaddrs *list = NULL;
    if (getifaddrs(&list) != 0)
        return -1;
    int prefix = -1;
    for (struct ifaddrs *a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || !a->ifa_netmask)
            continue;
        if (strcmp(a->ifa_name, ifname) != 0)
            continue;
        unsigned mask = ntohl(((struct sockaddr_in *)a->ifa_netmask)->sin_addr.s_addr);
        prefix = 0;
        while (mask & 0x80000000u) {
            prefix++;
            mask <<= 1;
        }
        break;
    }
    freeifaddrs(list);
    return prefix;
}
