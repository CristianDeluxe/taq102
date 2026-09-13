#include "master_find_peer_error.h"

#include <errno.h>

int master_find_peer_error(int error) {
    return error == ECONNREFUSED || error == ETIMEDOUT ||
           error == EHOSTUNREACH || error == ECONNRESET;
}
