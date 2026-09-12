// SOURCES: desk_tap.c
// Four even taps commit the interval from the second on; uneven taps commit
// the median of the last four; a bounce is ignored; a pause seeds again.
#include <assert.h>
#include <stdio.h>

#include "desk_tap.h"

int main(void) {
    struct desk_tap t;
    desk_tap_init(&t);
    assert(desk_tap_down(&t, 1000) == 0);
    assert(desk_tap_down(&t, 1500) == 500);
    assert(desk_tap_down(&t, 2000) == 500);
    assert(desk_tap_down(&t, 2500) == 500);
    // Uneven: intervals 480, 520, 500, 505 -> sorted 480 500 505 520 -> 502.
    desk_tap_reset(&t);
    assert(desk_tap_down(&t, 10000) == 0);
    assert(desk_tap_down(&t, 10480) == 480);
    assert(desk_tap_down(&t, 11000) == 500);
    assert(desk_tap_down(&t, 11500) == 500);
    assert(desk_tap_down(&t, 12005) == 502);
    // Only the last four count: 480 falls out.
    assert(desk_tap_down(&t, 12505) == 502);   // 520 500 505 500 -> 502
    assert(desk_tap_down(&t, 13005) == 500);   // 500 505 500 500 -> 500
    // A bounce under 200 ms neither commits nor moves the clock.
    assert(desk_tap_down(&t, 13100) == 0);
    assert(desk_tap_down(&t, 13505) == 500);
    // Two and a half seconds of silence: the next tap seeds.
    assert(desk_tap_down(&t, 16100) == 0);
    assert(desk_tap_down(&t, 16700) == 600);
    printf("desk_tap ok\n");
    return 0;
}
