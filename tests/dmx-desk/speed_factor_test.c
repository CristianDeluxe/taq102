// SOURCES: speed_factor.c
// The engine's table, mirrored: a sixteenth is 62 thousandths, one is 1000,
// sixteen is 16000; None is untouched and Zero is zero; 11 is outside.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "speed_factor.h"

int main(void) {
    assert(speed_factor_thousandths(2) == 62);
    assert(speed_factor_thousandths(6) == 1000);
    assert(speed_factor_thousandths(10) == 16000);
    assert(speed_factor_thousandths(0) == -1);
    assert(speed_factor_thousandths(1) == 0);
    assert(speed_factor_thousandths(11) == -1 && !speed_factor_valid(11));
    assert(strcmp(speed_factor_name(2), "1/16") == 0);
    assert(strcmp(speed_factor_name(6), "1") == 0);
    assert(strcmp(speed_factor_name(0), "None") == 0);
    assert(strcmp(speed_factor_name(-1), "?") == 0);
    printf("speed_factor ok\n");
    return 0;
}
