#ifndef POWER_POLICY_H
#define POWER_POLICY_H

#include <stdint.h>

struct status;
struct power_policy;

struct power_policy *power_policy_new(int max_level, int64_t now_ms);
int power_policy_step(struct power_policy *policy, const struct status *status,
                      int64_t now_ms, int asleep);
int power_policy_level(const struct power_policy *policy);
const char *power_policy_note(const struct power_policy *policy);
void power_policy_free(struct power_policy *policy);

#endif
