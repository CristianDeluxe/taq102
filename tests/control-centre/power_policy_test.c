// SOURCES: power_policy.c
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "power_policy.h"
#include "status.h"

static struct status reading(int usb, int ac, int ua, const char *word) {
    struct status status;
    memset(&status, 0, sizeof status);
    status.usb_online = usb;
    status.ac_online = ac;
    status.usb_valid = status.ac_valid = status.online_valid = 1;
    status.plugged = usb || ac;
    status.current_ua = ua;
    status.current_valid = status.word_valid = 1;
    status.cap = 80;
    status.cap_valid = 1;
    snprintf(status.word, sizeof status.word, "%s", word);
    return status;
}

static int sample(struct power_policy *policy, struct status *status,
                  int64_t *now, int64_t advance) {
    *now += advance;
    status->read_ms = *now;
    return power_policy_step(policy, status, *now, 0);
}

static int period(struct power_policy *policy, struct status status, int64_t *now) {
    int result = -1;
    for (int i = 0; i < 3; ++i) {
        int next = sample(policy, &status, now, 10000);
        if (next >= 0) result = next;
    }
    return result;
}

int main(void) {
    int64_t now = 0;
    struct power_policy *cadence = power_policy_new(255, now);
    struct status cadence_battery = reading(0, 0, -300000, "Discharging");
    int cadence_result = -1;
    for (int i = 0; i < 4; ++i) {
        int result = sample(cadence, &cadence_battery, &now, 9900);
        if (result >= 0) cadence_result = result;
    }
    assert(cadence_result == 128);
    power_policy_free(cadence);

    now = 0;
    struct power_policy *policy = power_policy_new(255, now);
    assert(policy != NULL);
    struct status battery = reading(0, 0, -300000, "Discharging");
    assert(sample(policy, &battery, &now, 10000) == -1);
    assert(sample(policy, &battery, &now, 10000) == -1);
    assert(sample(policy, &battery, &now, 10000) == 128);
    assert(period(policy, reading(1, 0, -200000, "Charging"), &now) == 40);
    assert(period(policy, reading(1, 0, 100001, "Charging"), &now) == 56);
    assert(period(policy, reading(1, 0, -50001, "Charging"), &now) == 40);
    assert(period(policy, reading(1, 0, 200000, "Charging"), &now) == -1);
    assert(power_policy_level(policy) == 40);
    assert(period(policy, reading(1, 0, -50000, "Charging"), &now) == -1);
    assert(period(policy, reading(1, 0, 100000, "Charging"), &now) == -1);

    struct status full = reading(1, 0, 0, "Full");
    assert(period(policy, full, &now) == 255);
    assert(period(policy, reading(1, 0, 0, "Charging"), &now) == 40);

    struct status invalid = reading(1, 0, 200000, "Charging");
    invalid.current_valid = 0;
    assert(period(policy, invalid, &now) == -1);
    invalid = reading(1, 0, 200000, "Charging");
    invalid.cap_valid = 0;
    assert(period(policy, invalid, &now) == -1);
    struct status fresh = reading(1, 0, 200000, "Charging");
    now += 10000;
    fresh.read_ms = now - 10001;
    assert(power_policy_step(policy, &fresh, now, 0) == -1);
    fresh.read_ms = now + 1;
    assert(power_policy_step(policy, &fresh, now, 0) == -1);
    fresh.read_ms = now;
    assert(power_policy_step(policy, &fresh, now, 0) == -1);
    assert(power_policy_step(policy, &fresh, now, 0) == -1);
    assert(power_policy_level(policy) == 40);
    fresh.word_valid = 0;
    now += 10000;
    fresh.read_ms = now;
    assert(power_policy_step(policy, &fresh, now, 0) == -1);

    fresh.word_valid = 1;
    for (int i = 0; i < 4; ++i) {
        ++now;
        fresh.read_ms = now;
        assert(power_policy_step(policy, &fresh, now, 0) == -1);
    }
    assert(power_policy_level(policy) == 40);

    assert(period(policy, reading(0, 1, 200000, "Charging"), &now) == -1);
    assert(power_policy_level(policy) == 40);
    assert(period(policy, reading(0, 1, 200000, "Charging"), &now) == 56);
    assert(period(policy, reading(1, 0, 200000, "Charging"), &now) == 40);
    now += 600000;
    assert(period(policy, reading(1, 0, 200000, "Charging"), &now) == 56);

    struct status drain = reading(1, 0, -200000, "Charging");
    assert(period(policy, drain, &now) == 40);
    assert(period(policy, drain, &now) == -1);
    assert(strcmp(power_policy_note(policy), "cannot sustain") == 0);

    struct status asleep = reading(1, 0, 200000, "Charging");
    now += 10000;
    asleep.read_ms = now;
    assert(power_policy_step(policy, &asleep, now, 1) == -1);
    assert(period(policy, asleep, &now) == -1);
    assert(power_policy_level(policy) == 40);
    assert(period(policy, asleep, &now) == -1);
    now += 600000;
    assert(period(policy, asleep, &now) == 56);

    power_policy_free(policy);
    assert(power_policy_new(7, 0) == NULL);
    policy = power_policy_new(24, 0);
    now = 0;
    assert(period(policy, reading(0, 0, -1, "Discharging"), &now) == 24);
    assert(period(policy, reading(1, 0, 1, "Charging"), &now) == -1);
    power_policy_free(policy);
    puts("power_policy ok");
    return 0;
}
