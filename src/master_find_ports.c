#include "master_find_ports.h"

int master_find_ports(int configured, int ports[MASTER_FIND_MAX_PORTS]) {
    if (configured < 1 || configured > 65535)
        return -1;
    ports[0] = configured;
    int count = 1;
    if (configured != 9998)
        ports[count++] = 9998;
    if (configured != 9999)
        ports[count++] = 9999;
    return count;
}
