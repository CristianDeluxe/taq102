// SOURCES: iface_prefix.c
// The loopback has an IPv4 address with a prefix of 8; an interface that
// does not exist has none.
#include <assert.h>
#include <stdio.h>

#include "iface_prefix.h"

int main(void) {
    int lo = iface_prefix("lo0");
    if (lo < 0)
        lo = iface_prefix("lo");
    assert(lo == 8);
    assert(iface_prefix("nosuch9") == -1);
    printf("iface_prefix ok\n");
    return 0;
}
