#include "desk_conf.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void desk_conf_defaults(struct desk_conf *c) {
    memset(c, 0, sizeof *c);
    c->port = 9999;
}

int desk_conf_valid_host(const char *host) {
    size_t n = host ? strlen(host) : 0;
    if (n == 0 || n >= DESK_CONF_HOST_MAX)
        return 0;
    for (size_t i = 0; i < n; i++) {
        char ch = host[i];
        if (!(isalnum((unsigned char)ch) || ch == '.' || ch == '-'))
            return 0;
    }
    return host[0] != '.' && host[n - 1] != '.';
}

int desk_conf_valid_port(int port) { return port >= 1 && port <= 65535; }

int desk_conf_load(struct desk_conf *c, const char *path) {
    desk_conf_defaults(c);
    FILE *f = fopen(path, "r");
    if (!f)
        return errno == ENOENT ? 0 : -1;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        const char *value = eq + 1;
        if (strcmp(line, "master") == 0 && desk_conf_valid_host(value))
            snprintf(c->master, sizeof c->master, "%s", value);
        else if (strcmp(line, "port") == 0) {
            char *end;
            long p = strtol(value, &end, 10);
            if (end != value && *end == '\0' && desk_conf_valid_port((int)p))
                c->port = (int)p;
        }
    }
    fclose(f);
    return 0;
}

int desk_conf_save(const struct desk_conf *c, const char *path) {
    if (c->master[0] && !desk_conf_valid_host(c->master))
        return -1;
    if (!desk_conf_valid_port(c->port))
        return -1;
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        perror(tmp);
        return -1;
    }
    FILE *f = fdopen(fd, "w");
    if (!f) {
        close(fd);
        return -1;
    }
    fprintf(f, "master=%s\nport=%d\n", c->master, c->port);
    if (fflush(f) != 0 || fsync(fd) != 0) {
        fclose(f);
        unlink(tmp);
        return -1;
    }
    fclose(f);
    if (rename(tmp, path) != 0) {
        perror(path);
        unlink(tmp);
        return -1;
    }
    return 0;
}
