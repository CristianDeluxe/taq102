// The show map: what the desk shows of the master's console, organised into
// pages and sections, generated beside the show by `qlctool deskmap` and never
// trusted: the map says what the generator meant, and validation against the
// console the master actually serves decides what the desk may press.
//
// Schema 2. Controls are stored in page order, then section order, then the
// section's own order, so a section is one contiguous run of the array.
#ifndef SHOWMAP_H
#define SHOWMAP_H

#include <stddef.h>
#include <stdint.h>

#define MAP_MAX_PAGES 8
#define MAP_MAX_SECTIONS 8
#define MAP_MAX_CONTROLS 256
#define MAP_KEY_MAX 48
#define MAP_CAPTION_MAX 48
#define MAP_MAX_SWATCHES 4

enum map_role {
    MAP_ROLE_STATE,     // the room's one state at a time
    MAP_ROLE_ACCENT,    // a hit held on the Mac; carried disabled
    MAP_ROLE_HAZE,      // an ambient fog rhythm
    MAP_ROLE_HOOK,      // a family's automatic owner
    MAP_ROLE_PICK,      // a latched manual choice within a family
    MAP_ROLE_CHASE,     // a dimmer chase
    MAP_ROLE_TOGGLE,    // any other toggle
};

struct map_control {
    char key[MAP_KEY_MAX];
    char caption[MAP_CAPTION_MAX];
    char detail[MAP_CAPTION_MAX];
    enum map_role role;
    int held;           // the Mac's button is a Flash: never pressed from here
    int widget_id;      // never -1 for a control
    int function_id;    // -1 when the generator recorded none
    int solo_id;        // the solo frame the widget must sit in, or -1
    int page, section;  // indices into the map's pages and that page's sections
    int enabled;        // the generator's word; validation may still disable
    char reason[MAP_CAPTION_MAX];
    uint32_t swatch[MAP_MAX_SWATCHES];   // 0xRRGGBB
    int swatches;
};

struct map_section {
    char key[MAP_KEY_MAX];
    char title[MAP_CAPTION_MAX];
    int solo_id;        // -1 when the section is not a solo frame
    int first, count;   // the run in show_map.control
};

struct map_page {
    char key[MAP_KEY_MAX];
    char title[MAP_CAPTION_MAX];
    struct map_section section[MAP_MAX_SECTIONS];
    int sections;
};

struct show_map {
    int schema;
    char qlc_version[16];
    char key[MAP_KEY_MAX];
    char workspace[128];
    char sha256[65];
    int grand_master_widget;    // -1 when the console has no GrandMaster slider
    int stop_all_widget;        // -1 when there is no StopAll button
    int stop_all_fade_ms;
    struct map_page page[MAP_MAX_PAGES];
    int pages;
    struct map_control control[MAP_MAX_CONTROLS];
    int count;
};

// Parses `len` bytes of map JSON. 0 on success, -1 on anything malformed, with
// the reason on stderr: a wrong schema, a section naming a control that is
// not there, a control no section lists, two controls on one widget, a swatch
// that is not #rrggbb, or any string past its bound.
int showmap_parse(const char *json, size_t len, struct show_map *out);

// Reads the file, with the same rules and a size cap.
int showmap_load(const char *path, struct show_map *out);

#endif
