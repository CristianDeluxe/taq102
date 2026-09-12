// SOURCES: wifi_join_network_id.c wifi_join_init.c wifi_join_finish.c wifi_join_advance.c wifi_join_restore.c wifi_join_fail.c wifi_join_start.c wifi_join_start_renewal.c wifi_join_event_matches.c wifi_join.c wifi_join_free.c wpa_ctrl_dial.c wpa_ctrl_transact.c wpa_ctrl.c wpa_ctrl_begin.c wpa_ctrl_request_fd.c wpa_ctrl_reply.c wpa_ctrl_abandon.c wpa_ctrl_request.c wpa_ctrl_event_fd.c wpa_ctrl_event.c wpa_ctrl_close.c wifi_conf.c action_worker.c
// A fake supplicant that keeps a network list, answers RECONFIGURE,
// LIST_NETWORKS and SELECT_NETWORK, and after a select pushes CONNECTED or
// a wrong-key disable, by mode. Joins advance only through step, with bounded
// calls, whole-file rollback, retained backups, and request-deadline failures.
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
#include <time.h>

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
    snprintf(k->path, sizeof k->path, "./output/dmxdesk-fake-join-%d", (int)getpid());
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
            strcpy(reply, k->mode == 4 && strcmp(cmd, "ENABLE_NETWORK all") == 0 ? "FAIL\n" : "OK\n");
        } else if (strcmp(cmd, "RECONFIGURE") == 0) {
            if (k->mode == 3)
                continue;
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
                // Deliver the event before the SELECT_NETWORK reply to
                // exercise the independent sockets' ordering explicitly.
                if (id == 0 && k->mode != 2 && k->mode != 1)
                    push(k, "<3>CTRL-EVENT-CONNECTED - Connection to 00:00:5e:00:53:01 completed [id=0 id_str=]");
                else if (k->mode == 0 || k->mode == 4)
                    push(k, "<3>CTRL-EVENT-CONNECTED - Connection to 00:00:5e:00:53:02 completed [id=1 id_str=]");
                else if (k->mode == 1) {
                    char event[256];
                    snprintf(event, sizeof event, "<3>CTRL-EVENT-SSID-TEMP-DISABLED id=%d reason=WRONG_KEY", id);
                    push(k, event);
                }
                sendto(k->fd, reply, strlen(reply), 0, (struct sockaddr *)&from, flen);
                continue;
            }
        }
        sendto(k->fd, reply, strlen(reply), 0, (struct sockaddr *)&from, flen);
    }
}

static const char *const QUIET_RENEW[] = { "/bin/sh", "-c", "true", NULL };

enum { POLL_MS = 50 };
static int64_t max_call_ms;

static int64_t now_ms(void) {
    struct timespec ts;
    assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static enum wifi_join_state step(struct wifi_join *j, int64_t offset,
                                 const char *event, const char *address) {
    int64_t begin = now_ms();
    enum wifi_join_state state = wifi_join_step(j, begin + offset, event, address);
    int64_t took = now_ms() - begin;
    if (took > max_call_ms) max_call_ms = took;
    assert(took < POLL_MS);
    return state;
}

static enum wifi_join_state drive(struct wifi_join *j, struct wpa_ctrl *c,
                                  int64_t offset, const char *address, int budget) {
    for (int i = 0; i < budget && j->running; i++) {
        struct pollfd p[2] = {
            { .fd = wpa_ctrl_event_fd(c), .events = POLLIN },
            { .fd = wpa_ctrl_request_fd(c), .events = POLLIN },
        };
        poll(p, 2, POLL_MS);
        char ev[512];
        const char *event = wpa_ctrl_event(c, ev, sizeof ev) == 1 ? ev : NULL;
        step(j, offset, event, address);
    }
    return j->state;
}

static void start(struct wifi_join *j, const char *ssid, const char *key,
                  int known, const char *previous) {
    int64_t begin = now_ms();
    assert(wifi_join_start(j, ssid, key, known, previous, begin) == 0);
    assert(now_ms() - begin < POLL_MS);
    assert(j->running && j->state == WIFI_JOIN_RUNNING);
    // start must not put even the first request on the socket.
    assert(!j->sent);
}

int main(void) {
    char conf[128];
    snprintf(conf, sizeof conf, "./output/dmxdesk-join-%d.conf", (int)getpid());
    unlink(conf);
    assert(wifi_conf_write_block(conf, "TestNet", "firstkey1", 1) == 0);
    char *original;
    size_t original_len;
    assert(wifi_conf_snapshot(conf, &original, &original_len) == 0);
    struct fake k;
    fake_open(&k);

    // Success, wrong key, association silence, replaced known block, known
    // block unchanged, and a daemon that never answers RECONFIGURE.
    for (int scenario = 0; scenario < 8; scenario++) {
        assert(wifi_conf_restore(conf, original, original_len) == 0);
        k.mode = scenario == 7 ? 4 : scenario == 0 ? 0 : scenario == 5 ? 3 : scenario == 1 || scenario == 3 || scenario == 6 ? 1 : 2;
        k.has_attached = 0;
        k.selected = -1;
        pid_t helper = fork();
        assert(helper >= 0);
        if (helper == 0) {
            alarm(30);
            for (;;) fake_service(&k, conf);
        }
        struct wpa_ctrl *c = wpa_ctrl_open(k.path);
        assert(c);
        struct wifi_join j;
        wifi_join_init(&j, c, conf);
        j.renew_argv = QUIET_RENEW;
        const char *ssid = scenario == 3 || scenario == 4 ? "TestNet" : "Cafe";
        struct stat before_st;
        assert(stat(conf, &before_st) == 0);
        int64_t began = now_ms();
        start(&j, ssid, scenario == 4 ? NULL : "examplekey", scenario == 4, "TestNet");
        int64_t offset = 0;
        if (scenario == 6) {
            char obstacle[160];
            snprintf(obstacle, sizeof obstacle, "%s.tmp", conf);
            assert(mkdir(obstacle, 0700) == 0);
            assert(drive(&j, c, 0, "", 30) == WIFI_JOIN_FAILED);
            assert(!j.running && j.restore_failed && j.before);
            assert(j.before_len == original_len && memcmp(j.before, original, original_len) == 0);
            char *backup = j.before;
            assert(wifi_join_start(&j, "Short", "abc", 0, "", now_ms()) == -1);
            assert(j.before == backup && j.restore_failed);
            assert(rmdir(obstacle) == 0);
            // A retry first restores the retained snapshot, even if the new
            // request itself is invalid and never reaches the daemon.
            assert(wifi_join_start(&j, "Short", "abc", 0, "", now_ms()) == -1);
            assert(!j.restore_failed && !j.before);
            char *restored;
            size_t len;
            assert(wifi_conf_snapshot(conf, &restored, &len) == 0);
            assert(len == original_len && memcmp(restored, original, len) == 0);
            free(restored);
        } else if (scenario == 0) {
            assert(drive(&j, c, 0, "", 20) == WIFI_JOIN_RUNNING);
            assert(strcmp(j.word, "Getting an address") == 0);
            assert(drive(&j, c, 0, "192.168.1.120", 10) == WIFI_JOIN_DONE);
            assert(!j.running);
            struct wifi_conf after;
            assert(wifi_conf_read(conf, &after) >= 0 && wifi_conf_knows(&after, "Cafe"));
            assert(wifi_conf_top_priority(&after) == 2);
        } else {
            if (scenario == 2 || scenario == 4) {
                assert(drive(&j, c, 0, "", 15) == WIFI_JOIN_RUNNING);
                assert(strcmp(j.word, "Associating") == 0);
                offset = WIFI_JOIN_STAGE_MS + 1;
                assert(step(&j, offset, NULL, "") == WIFI_JOIN_FAILED);
                assert(strcmp(j.reason, "No association") == 0);
            } else if (scenario == 5) {
                // Stop at the initial failure, before waiting for daemon
                // recovery: the on-disk rollback must already be complete.
                while (j.state == WIFI_JOIN_RUNNING && now_ms() - began < 4000)
                    drive(&j, c, 0, "", 1);
                int64_t elapsed = now_ms() - began;
                assert(j.state == WIFI_JOIN_FAILED && j.running);
                assert(elapsed >= 3000 && elapsed < 3500);
                assert(strstr(j.reason, "RECONFIGURE timed out"));
                printf("silent RECONFIGURE: failed and restored at %lld ms\n", (long long)elapsed);
            } else if (scenario == 7) {
                assert(drive(&j, c, 0, "192.168.1.120", 30) == WIFI_JOIN_FAILED);
                assert(strcmp(j.reason, "ENABLE_NETWORK all refused") == 0);
            } else {
                assert(drive(&j, c, 0, "", 30) == WIFI_JOIN_FAILED);
                assert(strcmp(j.reason, "Wrong key") == 0);
            }
            char *restored;
            size_t restored_len;
            assert(wifi_conf_snapshot(conf, &restored, &restored_len) == 0);
            assert(restored_len == original_len && memcmp(restored, original, original_len) == 0);
            free(restored);
            // The failed outcome retains ownership while recovery is active.
            assert(drive(&j, c, offset, "", 80) == WIFI_JOIN_FAILED);
            assert(!j.running);
            if (scenario != 5) {
                char buf[4096];
                assert(wpa_ctrl_request(c, "LIST_NETWORKS", buf, sizeof buf, 500) > 0);
                assert(strstr(buf, "0\tTestNet\tany\t[CURRENT]"));
            } else {
                assert(strstr(j.reason, "recovery timed out"));
            }
        }
        if (scenario == 4) {
            struct stat after_st;
            assert(stat(conf, &after_st) == 0 && before_st.st_ino == after_st.st_ino &&
                   before_st.st_mtime == after_st.st_mtime);
        }
        // Invalid keys are refused without a request or a changed file.
        assert(wifi_join_start(&j, "Short", "abc", 0, "TestNet", now_ms()) == -1);
        struct wifi_conf after;
        assert(wifi_conf_read(conf, &after) >= 0 && !wifi_conf_knows(&after, "Short"));
        wifi_join_free(&j);
        wpa_ctrl_close(c);
        kill(helper, SIGKILL);
        waitpid(helper, NULL, 0);
    }
    free(original);
    close(k.fd);
    unlink(k.path);
    unlink(conf);
    printf("wifi_join ok; longest step %lld ms (poll %d ms)\n", (long long)max_call_ms, POLL_MS);
    return 0;
}
