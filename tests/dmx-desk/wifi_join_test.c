// SOURCES: wifi_join.c wpa_ctrl.c wifi_conf.c action_worker.c
// A fake supplicant that keeps a network list, answers RECONFIGURE,
// LIST_NETWORKS and SELECT_NETWORK, and after a select pushes CONNECTED or
// a wrong-key disable, by mode. Three joins: one succeeds and the block
// stays; one fails on the key and the block is gone with the previous
// network selected again; one never associates and times out the same way.
#include <assert.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "wifi_conf.h"
#include "wifi_join.h"
#include "wpa_ctrl.h"

struct fake {
    int fd;
    char path[108];
    struct sockaddr_un attached;
    int has_attached;
    char names[8][40];
    int count;
    int selected;
    int mode;               // 0 connect, 1 wrong key, 2 silence
    char log[4096];
};

static void fake_open(struct fake *k) {
    memset(k, 0, sizeof *k);
    snprintf(k->path, sizeof k->path, "/tmp/dmxdesk-fake-join-%d", (int)getpid());
    unlink(k->path);
    k->fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    assert(k->fd >= 0);
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    strcpy(a.sun_path, k->path);
    assert(bind(k->fd, (struct sockaddr *)&a, sizeof a) == 0);
    strcpy(k->names[0], "TestNet");
    k->count = 1;
    k->selected = -1;
}

// RECONFIGURE re-reads the config: the fake reads the same file the join
// writes, so the list is what a real supplicant would hold.
static void reload(struct fake *k, const char *conf) {
    struct wifi_conf c;
    assert(wifi_conf_read(conf, &c) >= 0);
    k->count = c.count;
    for (int i = 0; i < c.count; i++)
        snprintf(k->names[i], sizeof k->names[i], "%s", c.network[i].ssid);
}

static void push(struct fake *k, const char *ev) {
    if (k->has_attached)
        sendto(k->fd, ev, strlen(ev), 0, (struct sockaddr *)&k->attached, sizeof k->attached);
}

static void fake_service(struct fake *k, const char *conf) {
    for (;;) {
        char cmd[256];
        struct sockaddr_un from;
        socklen_t flen = sizeof from;
        struct pollfd p = { .fd = k->fd, .events = POLLIN, .revents = 0 };
        if (poll(&p, 1, 5) <= 0)
            return;
        ssize_t n = recvfrom(k->fd, cmd, sizeof cmd - 1, 0, (struct sockaddr *)&from, &flen);
        if (n <= 0)
            return;
        cmd[n] = '\0';
        strncat(k->log, cmd, sizeof k->log - strlen(k->log) - 2);
        strncat(k->log, ";", sizeof k->log - strlen(k->log) - 1);
        char reply[1024] = "FAIL\n";
        if (strcmp(cmd, "ATTACH") == 0) {
            k->attached = from;
            k->has_attached = 1;
            strcpy(reply, "OK\n");
        } else if (strcmp(cmd, "DETACH") == 0 || strcmp(cmd, "ENABLE_NETWORK all") == 0) {
            strcpy(reply, "OK\n");
        } else if (strcmp(cmd, "RECONFIGURE") == 0) {
            reload(k, conf);
            strcpy(reply, "OK\n");
        } else if (strcmp(cmd, "LIST_NETWORKS") == 0) {
            strcpy(reply, "network id / ssid / bssid / flags\n");
            for (int i = 0; i < k->count; i++) {
                char line[80];
                snprintf(line, sizeof line, "%d\t%s\tany\t%s\n", i, k->names[i],
                         i == k->selected ? "[CURRENT]" : "");
                strcat(reply, line);
            }
        } else if (strncmp(cmd, "SELECT_NETWORK ", 15) == 0) {
            int id = atoi(cmd + 15);
            if (id < 0 || id >= k->count) {
                strcpy(reply, "FAIL\n");
            } else {
                k->selected = id;
                strcpy(reply, "OK\n");
                sendto(k->fd, reply, strlen(reply), 0, (struct sockaddr *)&from, flen);
                // The events a real daemon would push after a select.
                if (id == 0)
                    push(k, "<3>CTRL-EVENT-CONNECTED - Connection to 00:00:5e:00:53:01 completed [id=0 id_str=]");
                else if (k->mode == 0)
                    push(k, "<3>CTRL-EVENT-CONNECTED - Connection to 00:00:5e:00:53:02 completed [id=1 id_str=]");
                else if (k->mode == 1)
                    push(k, "<3>CTRL-EVENT-SSID-TEMP-DISABLED id=1 ssid=\"TestNet5\" auth_failures=1 duration=10 reason=WRONG_KEY");
                continue;
            }
        }
        sendto(k->fd, reply, strlen(reply), 0, (struct sockaddr *)&from, flen);
    }
}

static const char *const QUIET_RENEW[] = { "/bin/sh", "-c", "true", NULL };

// Drives the join until it settles or `budget` steps pass, feeding events
// from the daemon and the address the caller says the interface has.
static enum wifi_join_state drive(struct wifi_join *j, struct wpa_ctrl *c, int64_t *now,
                                  const char *address, int budget) {
    enum wifi_join_state st = WIFI_JOIN_RUNNING;
    for (int i = 0; i < budget && st == WIFI_JOIN_RUNNING; i++) {
        struct pollfd p = { .fd = wpa_ctrl_event_fd(c), .events = POLLIN, .revents = 0 };
        poll(&p, 1, 10);
        char ev[512];
        const char *event = wpa_ctrl_event(c, ev, sizeof ev) == 1 ? ev : NULL;
        *now += 10;
        st = wifi_join_step(j, *now, event, address);
    }
    return st;
}

int main(void) {
    char conf[128];
    snprintf(conf, sizeof conf, "/tmp/dmxdesk-join-%d.conf", (int)getpid());
    unlink(conf);
    assert(wifi_conf_write_block(conf, "TestNet", "firstkey1", 1) == 0);

    struct fake k;
    fake_open(&k);
    // The fake needs to be told its mode before the helper forks, so one
    // helper per scenario; the socket path stays.
    struct wpa_ctrl *c = NULL;
    int64_t now = 1000;

    // Scenario 1: the key is right, an address arrives: DONE, block kept.
    pid_t helper = fork();
    assert(helper >= 0);
    if (helper == 0) {
        for (int i = 0; i < 2000; i++)
            fake_service(&k, conf);
        _exit(0);
    }
    c = wpa_ctrl_open(k.path);
    assert(c);
    struct wifi_join j;
    wifi_join_init(&j, c, conf);
    j.renew_argv = QUIET_RENEW;
    assert(wifi_join_start(&j, "TestNet5", "correct horse", 0, "TestNet", now) == 0);
    assert(j.state == WIFI_JOIN_RUNNING && strcmp(j.word, "Associating") == 0);
    // No address yet: it sits in the address stage after CONNECTED.
    assert(drive(&j, c, &now, "", 30) == WIFI_JOIN_RUNNING);
    assert(strcmp(j.word, "Getting an address") == 0);
    assert(drive(&j, c, &now, "192.168.1.120", 5) == WIFI_JOIN_DONE);
    struct wifi_conf after;
    assert(wifi_conf_read(conf, &after) >= 0 && wifi_conf_knows(&after, "TestNet5"));
    assert(wifi_conf_top_priority(&after) == 2);
    wifi_join_free(&j);
    wpa_ctrl_close(c);
    kill(helper, 9);
    waitpid(helper, NULL, 0);

    // Scenario 2: wrong key: FAILED with the reason, block removed, the
    // previous network selected again.
    assert(wifi_conf_remove(conf, "TestNet5") == 0);
    k.mode = 1;
    k.has_attached = 0;
    helper = fork();
    assert(helper >= 0);
    if (helper == 0) {
        for (int i = 0; i < 2000; i++)
            fake_service(&k, conf);
        _exit(0);
    }
    c = wpa_ctrl_open(k.path);
    assert(c);
    wifi_join_init(&j, c, conf);
    j.renew_argv = QUIET_RENEW;
    assert(wifi_join_start(&j, "TestNet5", "wrong horse", 0, "TestNet", now) == 0);
    assert(drive(&j, c, &now, "", 30) == WIFI_JOIN_FAILED);
    assert(strcmp(j.reason, "Wrong key") == 0);
    assert(wifi_conf_read(conf, &after) >= 0 && !wifi_conf_knows(&after, "TestNet5"));
    assert(wifi_conf_knows(&after, "TestNet"));
    // The rollback asked for the previous network by its id.
    char buf[4096];
    assert(wpa_ctrl_request(c, "LIST_NETWORKS", buf, sizeof buf, 500) > 0);
    assert(strstr(buf, "0\tTestNet\tany\t[CURRENT]"));
    wifi_join_free(&j);
    wpa_ctrl_close(c);
    kill(helper, 9);
    waitpid(helper, NULL, 0);

    // Scenario 3: silence: the association stage times out and rolls back.
    k.mode = 2;
    k.has_attached = 0;
    k.selected = -1;
    helper = fork();
    assert(helper >= 0);
    if (helper == 0) {
        for (int i = 0; i < 2000; i++)
            fake_service(&k, conf);
        _exit(0);
    }
    c = wpa_ctrl_open(k.path);
    assert(c);
    wifi_join_init(&j, c, conf);
    j.renew_argv = QUIET_RENEW;
    assert(wifi_join_start(&j, "Cafe", NULL, 0, "TestNet", now) == 0);
    assert(drive(&j, c, &now, "", 10) == WIFI_JOIN_RUNNING);
    now += WIFI_JOIN_STAGE_MS + 1;
    assert(wifi_join_step(&j, now, NULL, "") == WIFI_JOIN_FAILED);
    assert(strcmp(j.reason, "No association") == 0);
    assert(wifi_conf_read(conf, &after) >= 0 && !wifi_conf_knows(&after, "Cafe"));
    // A key that breaks the rules never reaches the file or the daemon.
    assert(wifi_join_start(&j, "Short", "abc", 0, "TestNet", now) == -1);
    assert(wifi_conf_read(conf, &after) >= 0 && !wifi_conf_knows(&after, "Short"));
    // A known network's block is used as it is: nothing written, and a
    // failure leaves it in place with its key.
    struct stat before_st, after_st;
    assert(stat(conf, &before_st) == 0);
    assert(wifi_join_start(&j, "TestNet", NULL, 1, "", now) == 0);
    assert(!j.wrote_block);
    now += WIFI_JOIN_STAGE_MS + 1;
    assert(wifi_join_step(&j, now, NULL, "") == WIFI_JOIN_FAILED);
    assert(stat(conf, &after_st) == 0 && before_st.st_mtime == after_st.st_mtime);
    assert(wifi_conf_read(conf, &after) >= 0 && wifi_conf_knows(&after, "TestNet"));
    wifi_join_free(&j);
    wpa_ctrl_close(c);
    kill(helper, 9);
    waitpid(helper, NULL, 0);

    close(k.fd);
    unlink(k.path);
    unlink(conf);
    printf("wifi_join ok\n");
    return 0;
}
