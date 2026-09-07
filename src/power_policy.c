#include "power_policy.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "status.h"

struct source_policy {
    int level;
    int failed_level;
    int64_t failed_at_ms;
    int64_t last_up_at_ms;
    int last_up_level;
};

struct power_policy {
    int max_level;
    int effective_level;
    int effective_source;
    struct source_policy source[2];
    int samples;
    int64_t current_sum;
    int current_ua[3];
    int next_sample;
    int64_t window_started_ms;
    int64_t last_read_ms;
    int window_source;
    int window_full;
    const char *note;
};

static int minimum(int value, int maximum) {
    return value < maximum ? value : maximum;
}

static void reset_window(struct power_policy *policy, int64_t now_ms) {
    policy->samples = 0;
    policy->current_sum = 0;
    policy->next_sample = 0;
    policy->window_started_ms = now_ms;
    policy->window_source = -2;
    policy->window_full = -1;
}

static int apply_level(struct power_policy *policy, int level, int source) {
    int changed = level != policy->effective_level;
    policy->effective_level = level;
    policy->effective_source = source;
    return changed ? level : -1;
}

struct power_policy *power_policy_new(int max_level, int64_t now_ms) {
    if (max_level < 8) return NULL;
    struct power_policy *policy = calloc(1, sizeof *policy);
    if (!policy) return NULL;
    policy->max_level = max_level;
    policy->effective_source = -2;
    policy->window_source = -2;
    policy->window_full = -1;
    policy->window_started_ms = now_ms;
    policy->note = "";
    return policy;
}

static int decide(struct power_policy *policy, int source, int average, int64_t now_ms) {
    struct source_policy *state = &policy->source[source];
    int floor = minimum(40, policy->max_level);
    policy->note = "";
    if (!state->level) state->level = floor;
    else if (average < -50000) {
        if (state->level == floor) policy->note = "cannot sustain";
        else {
            int previous = state->level;
            state->level = state->level - 16 < floor ? floor : state->level - 16;
            if (state->last_up_level == previous && now_ms - state->last_up_at_ms <= 60000) {
                state->failed_level = state->last_up_level;
                state->failed_at_ms = now_ms;
                state->last_up_level = 0;
            }
        }
    } else if (average > 100000 && state->level < policy->max_level) {
        int candidate = state->level + 16;
        if (candidate > policy->max_level) candidate = policy->max_level;
        if (!(candidate == state->failed_level && now_ms - state->failed_at_ms < 600000)) {
            state->level = candidate;
            state->last_up_at_ms = now_ms;
            state->last_up_level = candidate;
        }
    }
    return apply_level(policy, state->level, source);
}

int power_policy_step(struct power_policy *policy, const struct status *status,
                      int64_t now_ms, int asleep) {
    if (!policy || !status) return -1;
    if (asleep) {
        reset_window(policy, now_ms);
        policy->last_read_ms = 0;
        return -1;
    }
    int64_t age = now_ms - status->read_ms;
    int valid = status->online_valid && status->usb_valid && status->ac_valid &&
                status->current_valid && status->cap_valid && status->word_valid &&
                age >= 0 && age <= 10000 &&
                (!policy->last_read_ms || status->read_ms > policy->last_read_ms);
    if (!valid) {
        reset_window(policy, now_ms);
        return -1;
    }
    if (policy->last_read_ms && status->read_ms - policy->last_read_ms > 15000)
        reset_window(policy, now_ms - 10000);
    else if (policy->last_read_ms && status->read_ms - policy->last_read_ms < 5000) {
        reset_window(policy, now_ms);
        policy->last_read_ms = status->read_ms;
        return -1;
    }
    policy->last_read_ms = status->read_ms;

    int source = !status->usb_online && !status->ac_online ? -1 : status->ac_online ? 1 : 0;
    int full = source >= 0 && strcmp(status->word, "Full") == 0;
    if ((policy->window_source != -2 && policy->window_source != source) ||
        (policy->window_full >= 0 && policy->window_full != full))
        reset_window(policy, now_ms - 10000);
    policy->window_source = source;
    policy->window_full = full;
    if (policy->samples == 3) {
        policy->current_sum -= policy->current_ua[policy->next_sample];
    } else {
        ++policy->samples;
    }
    policy->current_ua[policy->next_sample] = status->current_ua;
    policy->current_sum += status->current_ua;
    policy->next_sample = (policy->next_sample + 1) % 3;
    if (policy->samples < 3 || now_ms - policy->window_started_ms < 30000) return -1;
    int average = (int)(policy->current_sum / policy->samples);
    reset_window(policy, now_ms);
    if (source < 0) {
        policy->note = "";
        return apply_level(policy, minimum(128, policy->max_level), -1);
    }
    if (full) {
        policy->note = "";
        return apply_level(policy, policy->max_level, source + 2);
    }
    return decide(policy, source, average, now_ms);
}

int power_policy_level(const struct power_policy *policy) {
    return policy ? policy->effective_level : -1;
}

const char *power_policy_note(const struct power_policy *policy) {
    return policy ? policy->note : "";
}

void power_policy_free(struct power_policy *policy) {
    free(policy);
}
