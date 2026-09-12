#include "wifi_join_network_id.h"
#include <stdlib.h>
#include <string.h>

int wifi_join_network_id(char *buf, const char *ssid) {
    char *save = NULL;
    for (char *line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *tab = strchr(line, '\t');
        if (!tab)
            continue;
        *tab = '\0';
        char *name = tab + 1;
        char *end = strchr(name, '\t');
        if (end)
            *end = '\0';
        if (strcmp(name, ssid) == 0) {
            char *tail;
            long id = strtol(line, &tail, 10);
            return tail != line && !*tail && id >= 0 && id <= 2147483647L ? (int)id : -1;
        }
    }
    return -1;
}
