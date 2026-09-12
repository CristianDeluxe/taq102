// SOURCES: desk_power.c backlight.c settings.c settings_store.c power_policy.c
// A fake backlight directory and a settings file in a temporary root: the
// level set is written to the panel now and to the file two seconds later;
// a restart reads it back; the aware switch is kept too.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "desk_power.h"

static void put(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    assert(f);
    fputs(text, f);
    fclose(f);
}

static int get(const char *path) {
    FILE *f = fopen(path, "r");
    assert(f);
    int v = -1;
    assert(fscanf(f, "%d", &v) == 1);
    fclose(f);
    return v;
}

int main(void) {
    char root[] = "/tmp/dmxdesk-power-XXXXXX";
    assert(mkdtemp(root));
    char bl[256], conf[256], node[256], maxp[256];
    snprintf(bl, sizeof bl, "%s/backlight", root);
    snprintf(node, sizeof node, "%s/backlight/backlight", root);
    snprintf(maxp, sizeof maxp, "%s/max_brightness", node);
    snprintf(conf, sizeof conf, "%s/taq102.conf", root);
    assert(mkdir(bl, 0755) == 0 && mkdir(node, 0755) == 0);
    put(maxp, "255\n");
    char cur[256];
    snprintf(cur, sizeof cur, "%s/brightness", node);
    put(cur, "120\n");

    struct desk_power p;
    assert(desk_power_init(&p, conf, bl, 1000) == 0);
    assert(p.backlight_ready && p.max == 255);
    // No file: the defaults apply to the panel at once.
    int def = p.level;
    assert(get(cur) == def);
    desk_power_set_level(&p, 200, 1000);
    assert(get(cur) == 200);
    struct status st;
    memset(&st, 0, sizeof st);
    desk_power_tick(&p, &st, 1500);
    assert(access(conf, F_OK) != 0);            // not yet
    desk_power_tick(&p, &st, 3100);
    assert(access(conf, F_OK) == 0 && !p.save_failed);
    desk_power_set_aware(&p, 1, 3100);
    desk_power_tick(&p, &st, 6000);
    desk_power_free(&p);

    // A restart reads both back; under the policy the panel's level stands.
    put(cur, "90\n");
    struct desk_power q;
    assert(desk_power_init(&q, conf, bl, 10000) == 0);
    assert(q.settings.brightness == 200 && desk_power_aware(&q) == 1);
    assert(q.level == 90);
    desk_power_set_aware(&q, 0, 10000);
    assert(get(cur) == 200);
    desk_power_free(&q);
    printf("desk_power ok\n");
    return 0;
}
