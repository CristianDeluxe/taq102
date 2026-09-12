#include "speed_factor.h"

static const int THOUSANDTHS[] = { -1, 0, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
static const char *const NAMES[] = { "None", "Zero", "1/16", "1/8", "1/4", "1/2", "1", "2", "4", "8", "16" };

int speed_factor_valid(int factor) {
    return factor >= 0 && factor <= SPEED_FACTOR_MAX;
}

int speed_factor_thousandths(int factor) {
    return speed_factor_valid(factor) ? THOUSANDTHS[factor] : -1;
}

const char *speed_factor_name(int factor) {
    return speed_factor_valid(factor) ? NAMES[factor] : "?";
}
