#include "desk_brightness_level_at.h"

int desk_brightness_level_at(int maximum, int x) {
    struct desk_rect track = desk_brightness_track();
    int span = track.w - 1;
    int offset = x - track.x;
    // Keep the screen visible: setup's minimum is 8, not blackout. Eight
    // pixels inside each painted end absorb finger placement error; the
    // interior keeps the painted scale, as with the master fader.
    if (maximum <= 8 || offset <= 8)
        return 8;
    if (offset >= span - 8)
        return maximum;
    return 8 + (int)(((long long)offset * (maximum - 8) + span / 2) / span);
}
