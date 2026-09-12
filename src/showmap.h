// The show map: which of the master's controls this desk shows, where, and
// under what name. Written by hand beside the show, parsed here, and never
// trusted: the map says what the operator wants, and validation against the
// loaded console decides what the desk may actually do.
//
// Positions are a row and a column, not pixels. The desk resolves them against
// its own grid, so a map cannot place two tiles on top of each other or push
// one under the master column.
#ifndef SHOWMAP_H
#define SHOWMAP_H

#include <stddef.h>

#define MAP_MAX_CONTROLS 32
#define MAP_LABEL_MAX 32
#define MAP_KEY_MAX 64

enum map_action { MAP_TOGGLE, MAP_MASTER, MAP_BLACKOUT };

struct map_control {
    char label[MAP_LABEL_MAX];
    enum map_action action;
    int row, col;       // grid slot; ignored by master and blackout
    int widget_id;      // -1 when the action needs no widget
    int function_id;    // -1 when it drives none
};

struct show_map {
    char key[MAP_KEY_MAX];
    char workspace[MAP_KEY_MAX];
    char sha256[65];
    struct map_control control[MAP_MAX_CONTROLS];
    int count;
};

// Parses `len` bytes of map JSON. 0 on success, -1 on anything malformed, with
// the reason on stderr. A map with no controls is malformed.
int showmap_parse(const char *json, size_t len, struct show_map *out);

// Reads the file, with the same rules and a size cap.
int showmap_load(const char *path, struct show_map *out);

#endif
