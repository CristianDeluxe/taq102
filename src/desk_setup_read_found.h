#ifndef DESK_SETUP_READ_FOUND_H
#define DESK_SETUP_READ_FOUND_H

#include <stdio.h>

#include "desk_setup.h"

// Consume a completed child's stream and exit status. NULL is missing output,
// not an empty successful sweep. Accept legacy bare IPv4 lines as well.
void desk_setup_read_found(struct desk_setup *s, FILE *stream, int exit_status);

#endif
