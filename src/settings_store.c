#include "settings_store.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int settings_store_available(const char *path) {
    char *parent = strdup(path);
    if (!parent) return 0;
    char *slash = strrchr(parent, '/');
    if (!slash) { free(parent); parent = strdup("."); }
    else if (slash == parent) slash[1] = 0;
    else *slash = 0;
    if (!parent) return 0;
    int available = access(parent, W_OK) == 0;
    if (strncmp(path, "/data/", 6) == 0) {
        struct stat data, root;
        available = available && stat("/data", &data) == 0 && stat("/", &root) == 0 &&
            data.st_dev != root.st_dev;
    }
    free(parent);
    return available;
}
