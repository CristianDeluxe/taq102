// SOURCES: sleep_state.c
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include "sleep_state.h"

static void test_sleep_deadline(void) {
    struct sleep_timer timer;

    sleep_timer_init(&timer, 1, 0);
    assert(!sleep_timer_due(&timer, 59999, 0, 1));
    assert(sleep_timer_due(&timer, 60000, 0, 1));
    assert(!sleep_timer_due(&timer, 60000, 1, 1));
    assert(!sleep_timer_due(&timer, 60000, 0, 0));

    sleep_timer_touch(&timer, 61000);
    assert(!sleep_timer_due(&timer, 120999, 0, 1));
    assert(sleep_timer_due(&timer, 121000, 0, 1));

    sleep_timer_set_minutes(&timer, 0, 121000);
    assert(!sleep_timer_due(&timer, INT64_MAX, 0, 1));

    sleep_timer_set_minutes(&timer, 1, INT64_MAX - 10);
    assert(timer.deadline_ms == INT64_MAX);
    assert(sleep_timer_due(&timer, INT64_MAX, 0, 1));
}

static void take_baseline(struct pickup *pickup, int x, int y, int z,
                          int64_t at_ms) {
    assert(!pickup_feed(pickup, x, y, z, at_ms));
    assert(pickup->have_base);
}

static void test_pickup_all_axes_and_threshold(void) {
    struct pickup pickup;

    pickup_start(&pickup, 0);
    assert(!pickup_feed(&pickup, 0, 0, 1000, 500));
    take_baseline(&pickup, 0, 0, 1000, 2000);

    assert(!pickup_feed(&pickup, 150, 0, 1000, 2100));
    assert(pickup.votes == 0);
    assert(!pickup_feed(&pickup, 151, 0, 1000, 2200));
    assert(!pickup_feed(&pickup, 151, 0, 1000, 2300));
    assert(pickup_feed(&pickup, 151, 0, 1000, 2400));

    pickup_start(&pickup, 3000);
    take_baseline(&pickup, 20, -30, 900, 5000);
    assert(!pickup_feed(&pickup, 20, 121, 900, 5100));
    assert(!pickup_feed(&pickup, 20, 121, 900, 5200));
    assert(pickup_feed(&pickup, 20, 121, 900, 5300));

    pickup_start(&pickup, 6000);
    take_baseline(&pickup, 20, -30, 900, 8000);
    assert(!pickup_feed(&pickup, 20, -30, 749, 8100));
    assert(!pickup_feed(&pickup, 20, -30, 749, 8200));
    assert(pickup_feed(&pickup, 20, -30, 749, 8300));
}

static void test_pickup_requires_fresh_consecutive_samples(void) {
    struct pickup pickup;

    pickup_start(&pickup, 0);
    take_baseline(&pickup, 0, 0, 0, 2000);
    assert(!pickup_feed(&pickup, 200, 0, 0, 2100));
    assert(!pickup_feed(&pickup, 200, 0, 0, 2100));
    assert(!pickup_feed(&pickup, 200, 0, 0, 2099));
    assert(pickup.votes == 1);
    assert(!pickup_feed(&pickup, 200, 0, 0, 2200));
    assert(!pickup_feed(&pickup, 0, 0, 0, 2300));
    assert(pickup.votes == 0);
    assert(!pickup_feed(&pickup, 200, 0, 0, 2400));
    assert(!pickup_feed(&pickup, 200, 0, 0, 2500));
    assert(pickup_feed(&pickup, 200, 0, 0, 2600));
}

static void test_pickup_restarts_and_subtracts_safely(void) {
    struct pickup pickup;

    pickup_start(&pickup, 3000);
    take_baseline(&pickup, 900, 0, 0, 5000);
    pickup_start(&pickup, 6000);
    assert(!pickup.have_base && pickup.votes == 0);
    take_baseline(&pickup, INT_MAX, INT_MIN, 0, 8000);
    assert(!pickup_feed(&pickup, INT_MIN, INT_MAX, 0, 8100));
    assert(!pickup_feed(&pickup, INT_MIN, INT_MAX, 0, 8200));
    assert(pickup_feed(&pickup, INT_MIN, INT_MAX, 0, 8300));
}

int main(void) {
    test_sleep_deadline();
    test_pickup_all_axes_and_threshold();
    test_pickup_requires_fresh_consecutive_samples();
    test_pickup_restarts_and_subtracts_safely();
    printf("sleep ok\n");
    return 0;
}
