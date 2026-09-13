#ifndef MASTER_FIND_PORTS_H
#define MASTER_FIND_PORTS_H

#define MASTER_FIND_MAX_PORTS 3

// Configured port first, then my 9998 and QLC+ 5.2.2's default 9999.
// Deduplicates in order; returns -1 for an invalid configured port.
int master_find_ports(int configured, int ports[MASTER_FIND_MAX_PORTS]);

#endif
