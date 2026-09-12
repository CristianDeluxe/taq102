// The desk's own file on /data: the master it talks to. Two lines,
// `master=<host>` and `port=<n>`, read at start below any `--host` given
// on the command line, and written when the operator picks a master.
#ifndef DESK_CONF_H
#define DESK_CONF_H

#define DESK_CONF_HOST_MAX 128

struct desk_conf {
    char master[DESK_CONF_HOST_MAX];   // empty when unset
    int port;                          // 9999 when unset
};

void desk_conf_defaults(struct desk_conf *c);
// 0 when read (a missing file is the defaults), -1 when the file exists and
// cannot be read. A malformed line is ignored, a bad port falls back.
int desk_conf_load(struct desk_conf *c, const char *path);
// Atomic: a temporary file and a rename. -1 with the reason on stderr.
int desk_conf_save(const struct desk_conf *c, const char *path);
// A host is an IPv4 address or a hostname of letters, digits, dots and
// dashes; a port is 1..65535.
int desk_conf_valid_host(const char *host);
int desk_conf_valid_port(int port);

#endif
