// QLC+ 5.2.2's speed multiplier enum, as the engine reads and caches it
// (qmlui/virtualconsole/vcspeeddial.h and cacheMultipliers): 0 None means
// the field is left alone, 1 Zero, 2..10 are 1/16, 1/8, 1/4, 1/2, 1, 2, 4,
// 8, 16. The engine keeps them in integer thousandths, so a sixteenth is
// 62/1000 to the show, and the desk mirrors that rather than promising 0.0625.
#ifndef SPEED_FACTOR_H
#define SPEED_FACTOR_H

#define SPEED_FACTOR_NONE 0
#define SPEED_FACTOR_ZERO 1
#define SPEED_FACTOR_MIN 2      // 1/16
#define SPEED_FACTOR_ONE 6
#define SPEED_FACTOR_MAX 10     // 16

int speed_factor_valid(int factor);
// Thousandths, as cached: -1 for None, 0 for Zero, 62 for 1/16 ... 16000.
int speed_factor_thousandths(int factor);
// "None", "Zero", "1/16" ... "16"; "?" outside the enum.
const char *speed_factor_name(int factor);

#endif
