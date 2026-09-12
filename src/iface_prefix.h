// The IPv4 prefix length of an interface, from its netmask, so the subnet
// sweep covers the network the tablet is actually on. -1 when the interface
// has no IPv4 address.
#ifndef IFACE_PREFIX_H
#define IFACE_PREFIX_H

int iface_prefix(const char *ifname);

#endif
