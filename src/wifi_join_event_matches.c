#include "wifi_join_event_matches.h"
#include <stdlib.h>
#include <string.h>

int wifi_join_event_matches(const struct wifi_join *j, const char *event) {
    const char *id = strstr(event, "id=");
    if (!id)
        return 0;
    char *end;
    long value = strtol(id + 3, &end, 10);
    return end != id + 3 && (*end == ' ' || *end == ']' || !*end) && value == j->network_id;
}
