#include "desk_setup_read_found.h"

#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

void desk_setup_read_found(struct desk_setup *s, FILE *stream, int exit_status) {
    char hosts[SETUP_FOUND_MAX][SETUP_HOST_MAX];
    int ports[SETUP_FOUND_MAX];
    int count = 0, partial = 0;
    int failed = exit_status != 0 || !stream;
    char line[SETUP_HOST_MAX];
    while (!failed && fgets(line, sizeof line, stream)) {
        char *newline = strchr(line, '\n');
        if (!newline || partial) {
            failed = 1;
            break;
        }
        *newline = '\0';
        if (strcmp(line, "partial") == 0) {
            partial = 1;
            continue;
        }
        int port = s->port;
        char *colon = strchr(line, ':');
        if (colon) {
            *colon++ = '\0';
            char *end;
            long value = strtol(colon, &end, 10);
            if (!*colon || *end || value < 1 || value > 65535 ||
                strspn(colon, "0123456789") != strlen(colon)) {
                failed = 1;
                break;
            }
            port = (int)value;
        }
        struct in_addr address;
        if (inet_pton(AF_INET, line, &address) != 1 || port < 1 || port > 65535) {
            failed = 1;
            break;
        }
        if (count < SETUP_FOUND_MAX) {
            snprintf(hosts[count], sizeof hosts[count], "%s", line);
            ports[count++] = port;
        }
    }
    if (stream && ferror(stream))
        failed = 1;
    desk_setup_set_found(s, hosts, failed ? 0 : count, failed ? 0 : partial);
    if (!failed)
        for (int i = 0; i < count; i++)
            s->found_port[i] = ports[i];
    s->master_busy[0] = '\0';
    if (strcmp(s->master_note, "Search stopped") != 0)
        snprintf(s->master_note, sizeof s->master_note, "%s",
                 failed ? "Could not sweep" : count ? "Sweep complete" :
                 partial ? "No QLC+ in scanned range" : "No QLC+ answered scanned ports");
}
