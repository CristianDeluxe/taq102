#include "desk_brightness_track.h"

struct desk_rect desk_brightness_track(void) {
    return (struct desk_rect){SETUP_FADER_X + 16, SETUP_FADER_Y + SETUP_FADER_H - 36,
                              SETUP_FADER_W - 32, 20};
}
