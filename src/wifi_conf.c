#include "wifi_conf.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CONF_MAX (64 * 1024)

static char *read_whole(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        *len = 0;
        return NULL;
    }
    char *buf = malloc(CONF_MAX + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, CONF_MAX, f);
    fclose(f);
    buf[n] = '\0';
    *len = n;
    return buf;
}

// Unquotes a `ssid="..."` value into out; returns 0 when it is not quoted
// or too long.
static int unquote(const char *value, char *out, size_t cap) {
    if (*value != '"')
        return -1;
    value++;
    size_t n = 0;
    while (*value && *value != '"') {
        if (*value == '\\' && value[1])
            value++;
        if (n + 1 >= cap)
            return -1;
        out[n++] = *value++;
    }
    out[n] = '\0';
    return *value == '"' ? 0 : -1;
}

// Finds the block for `ssid`: sets *start and *end to the byte range of
// "network={ ... }\n", or returns 0 when there is none.
static int find_block(const char *text, const char *ssid, size_t *start, size_t *end) {
    const char *p = text;
    while ((p = strstr(p, "network={")) != NULL) {
        const char *close = strchr(p, '}');
        if (!close)
            return 0;
        const char *s = strstr(p, "ssid=");
        if (s && s < close) {
            char found[WIFI_CONF_SSID_MAX];
            if (unquote(s + 5, found, sizeof found) == 0 && strcmp(found, ssid) == 0) {
                // The block starts at the line's own start.
                const char *line = p;
                while (line > text && line[-1] != '\n')
                    line--;
                const char *after = close + 1;
                if (*after == '\n')
                    after++;
                *start = (size_t)(line - text);
                *end = (size_t)(after - text);
                return 1;
            }
        }
        p = close + 1;
    }
    return 0;
}

int wifi_conf_read(const char *path, struct wifi_conf *out) {
    memset(out, 0, sizeof *out);
    size_t len;
    char *text = read_whole(path, &len);
    if (!text)
        return errno == ENOENT ? 0 : -1;
    const char *p = text;
    while ((p = strstr(p, "network={")) != NULL && out->count < WIFI_CONF_MAX_NETWORKS) {
        const char *close = strchr(p, '}');
        if (!close)
            break;
        struct wifi_known *k = &out->network[out->count];
        const char *s = strstr(p, "ssid=");
        if (s && s < close && unquote(s + 5, k->ssid, sizeof k->ssid) == 0) {
            const char *pr = strstr(p, "priority=");
            k->priority = pr && pr < close ? atoi(pr + 9) : 0;
            out->count++;
        }
        p = close + 1;
    }
    free(text);
    return out->count;
}

int wifi_conf_knows(const struct wifi_conf *conf, const char *ssid) {
    for (int i = 0; i < conf->count; i++)
        if (strcmp(conf->network[i].ssid, ssid) == 0)
            return 1;
    return 0;
}

int wifi_conf_top_priority(const struct wifi_conf *conf) {
    int top = 0;
    for (int i = 0; i < conf->count; i++)
        if (conf->network[i].priority > top)
            top = conf->network[i].priority;
    return top;
}

static int valid_ssid(const char *ssid) {
    size_t n = strlen(ssid);
    if (n == 0 || n >= WIFI_CONF_SSID_MAX)
        return 0;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)ssid[i] < 0x20 || ssid[i] == 0x7f)
            return 0;
    return 1;
}

static int valid_psk(const char *psk) {
    size_t n = strlen(psk);
    if (n < 8 || n > 63)
        return 0;
    for (size_t i = 0; i < n; i++)
        if (psk[i] < 0x20 || psk[i] > 0x7e)
            return 0;
    return 1;
}

// Writes `s` quoted the way wpa_supplicant reads it: backslash and quote
// escaped.
static void quoted(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\')
            fputc('\\', f);
        fputc(*s, f);
    }
    fputc('"', f);
}

// Replaces the file's content atomically with `head` + block + `tail`.
static int write_atomic(const char *path, const char *head, size_t head_len,
                        const char *ssid, const char *psk, int priority,
                        const char *tail, size_t tail_len) {
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) {
        perror(tmp);
        return -1;
    }
    FILE *f = fdopen(fd, "w");
    if (!f) {
        close(fd);
        return -1;
    }
    if (head_len)
        fwrite(head, 1, head_len, f);
    if (ssid) {
        if (head_len && head[head_len - 1] != '\n')
            fputc('\n', f);
        fputs("network={\n\tssid=", f);
        quoted(f, ssid);
        fputc('\n', f);
        if (psk) {
            fputs("\tpsk=", f);
            quoted(f, psk);
            fputc('\n', f);
        } else {
            fputs("\tkey_mgmt=NONE\n", f);
        }
        fprintf(f, "\tpriority=%d\n}\n", priority);
    }
    if (tail_len)
        fwrite(tail, 1, tail_len, f);
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

int wifi_conf_write_block(const char *path, const char *ssid, const char *psk, int priority) {
    if (!path || !ssid || !valid_ssid(ssid) || (psk && !valid_psk(psk))) {
        fprintf(stderr, "wifi: refusing to write a network with an unusable name or key\n");
        return -1;
    }
    size_t len;
    char *text = read_whole(path, &len);
    if (!text && errno != ENOENT)
        return -1;
    const char *body = text ? text : "";
    size_t start = len, end = len;
    find_block(body, ssid, &start, &end);
    int rc = write_atomic(path, body, start, ssid, psk, priority, body + end, len - end);
    free(text);
    return rc;
}

int wifi_conf_remove(const char *path, const char *ssid) {
    size_t len;
    char *text = read_whole(path, &len);
    if (!text)
        return errno == ENOENT ? 0 : -1;
    size_t start, end;
    if (!find_block(text, ssid, &start, &end)) {
        free(text);
        return 0;
    }
    int rc = write_atomic(path, text, start, NULL, NULL, 0, text + end, len - end);
    free(text);
    return rc;
}
