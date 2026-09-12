// SOURCES: wifi_conf.c
// The supplicant's file, kept whole: a block added, replaced, removed, with
// the lines around it untouched and the key never in the desk's own output.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "wifi_conf.h"

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    assert(f);
    static char buf[8192];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

int main(void) {
    char path[256];
    snprintf(path, sizeof path, "/tmp/dmxdesk-wifi-%d.conf", (int)getpid());
    unlink(path);

    // A missing file is an empty list, and the first block creates it.
    struct wifi_conf conf;
    assert(wifi_conf_read(path, &conf) == 0 && conf.count == 0);
    assert(wifi_conf_write_block(path, "TestNet", "correct horse battery", 1) == 0);
    {
        // A file without a control interface line gets one, first, once.
        FILE *r = fopen(path, "r");
        char first[80] = "";
        assert(r && fgets(first, sizeof first, r));
        fclose(r);
        assert(strcmp(first, "ctrl_interface=/var/run/wpa_supplicant\n") == 0);
    }
    assert(wifi_conf_read(path, &conf) == 1 && strcmp(conf.network[0].ssid, "TestNet") == 0);
    assert(conf.network[0].priority == 1 && wifi_conf_knows(&conf, "TestNet"));

    // A comment and a global line survive around the blocks.
    FILE *f = fopen(path, "w");
    fputs("# written by hand\nctrl_interface=/var/run/wpa_supplicant\nupdate_config=0\n"
          "network={\n\tssid=\"TestNet\"\n\tpsk=\"correct horse battery\"\n\tpriority=1\n}\n", f);
    fclose(f);
    assert(wifi_conf_write_block(path, "Venue \"quoted\" \\ name", NULL, 2) == 0);
    char *text = slurp(path);
    assert(strstr(text, "# written by hand\nctrl_interface=/var/run/wpa_supplicant\nupdate_config=0\n"));
    assert(strstr(text, "ssid=\"Venue \\\"quoted\\\" \\\\ name\"\n\tkey_mgmt=NONE\n\tpriority=2\n"));
    assert(strstr(text, "psk=\"correct horse battery\""));
    assert(wifi_conf_read(path, &conf) == 2 && wifi_conf_top_priority(&conf) == 2);
    assert(strcmp(conf.network[1].ssid, "Venue \"quoted\" \\ name") == 0);

    // Replacing keeps the position and drops the old key.
    assert(wifi_conf_write_block(path, "TestNet", "another key here", 3) == 0);
    text = slurp(path);
    assert(!strstr(text, "correct horse") && strstr(text, "psk=\"another key here\""));
    assert(strstr(text, "update_config=0\nnetwork={\n\tssid=\"TestNet\""));
    assert(wifi_conf_read(path, &conf) == 2 && conf.network[0].priority == 3);

    // Removing one leaves the other whole; removing an absent one is fine.
    assert(wifi_conf_remove(path, "Venue \"quoted\" \\ name") == 0);
    text = slurp(path);
    assert(!strstr(text, "Venue") && strstr(text, "TestNet") && strstr(text, "# written by hand"));
    assert(wifi_conf_remove(path, "nobody") == 0);
    assert(wifi_conf_read(path, &conf) == 1);

    // Bounds: a short key, a key with a control character, an empty SSID.
    assert(wifi_conf_write_block(path, "X", "short", 1) == -1);
    assert(wifi_conf_write_block(path, "X", "has a tab\tin it", 1) == -1);
    assert(wifi_conf_write_block(path, "", "long enough key", 1) == -1);
    assert(wifi_conf_read(path, &conf) == 1);       // untouched by the refusals

    unlink(path);
    printf("wifi_conf ok\n");
    return 0;
}
