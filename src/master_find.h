// The subnet sweep for a running QLC+: every host of the tablet's own
// network (capped) gets a non-blocking connect on the web port, in batches
// with a deadline, and those that answer `GET /` with `QLC+` in the first
// two kilobytes are masters. Runs as `dmxdesk --find <addr>/<prefix> <port>`
// in a child process, printing one host per line, so the desk's loop never
// waits on it. Nothing in QLC+ announces itself, so this is the only
// autodetection there is.
#ifndef MASTER_FIND_H
#define MASTER_FIND_H

#include <stdio.h>

#define MASTER_FIND_MAX_HOSTS 256
#define MASTER_FIND_BATCH 64
#define MASTER_FIND_BATCH_MS 400

// Sweeps the network of `cidr` ("192.168.1.71/24") on `port`, skipping the
// address itself, printing each host that answers as QLC+ on `out`, and a
// final line `partial` when the network was larger than the cap. Returns
// the number of masters found, or -1 for an unusable cidr.
int master_find_run(const char *cidr, int port, FILE *out);

#endif
