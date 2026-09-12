// SOURCES: desk_conf.c
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "desk_conf.h"

int main(void) {
    char path[256];
    snprintf(path, sizeof path, "/tmp/dmxdesk-conf-%d", (int)getpid());
    unlink(path);
    struct desk_conf c;
    assert(desk_conf_load(&c, path) == 0 && c.master[0] == '\0' && c.port == 9999);
    snprintf(c.master, sizeof c.master, "192.168.1.65");
    c.port = 9998;
    assert(desk_conf_save(&c, path) == 0);
    struct desk_conf back;
    assert(desk_conf_load(&back, path) == 0);
    assert(strcmp(back.master, "192.168.1.65") == 0 && back.port == 9998);
    // A malformed file keeps the defaults for what it cannot read.
    FILE *f = fopen(path, "w");
    fputs("master=bad host name\nport=99999\njunk\n", f);
    fclose(f);
    assert(desk_conf_load(&back, path) == 0 && back.master[0] == '\0' && back.port == 9999);
    assert(desk_conf_valid_host("macbook-pro.local") && !desk_conf_valid_host("a b") &&
           !desk_conf_valid_host("") && !desk_conf_valid_host(".x"));
    assert(desk_conf_valid_port(1) && desk_conf_valid_port(65535) && !desk_conf_valid_port(0));
    c.port = 0;
    assert(desk_conf_save(&c, path) == -1);
    unlink(path);
    printf("desk_conf ok\n");
    return 0;
}
