// SOURCES: desk_rebuild_model.c desk_model.c desk_master_level_at.c showmap_validate.c showmap.c vcjson.c
// Tabs and console refreshes keep every page's bank, including hidden pages.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "desk_rebuild_model.h"

int main(void) {
    struct desk_model m;
    struct show_map map;
    struct vc_doc console = {0};
    assert(showmap_load("show/vibra.desk.json", &map) == 0);
    struct desk_layout layout = { .pages = 3, .banks = {1, 3, 2} };
    desk_init(&m);
    desk_set_layout(&m, &layout);
    desk_set_view(&m, 1, 2);
    desk_set_view(&m, 0, 0);
    desk_set_view(&m, 1, -1);
    assert(m.page == 1 && m.bank == 2 && m.page_bank[1] == 2);
    desk_set_view(&m, 2, 1);
    desk_set_view(&m, 0, 0);
    desk_rebuild_model(&m, &map, &console, &layout);
    assert(m.page == 0 && m.bank == 0);
    desk_set_view(&m, 1, -1);
    assert(m.bank == 2);
    desk_set_view(&m, 2, -1);
    assert(m.bank == 1);
    desk_rebuild_model(&m, &map, &console, &layout);
    assert(m.page == 2 && m.bank == 1);

    // Hidden banks clamp too, and a removed page cannot retain a stale bank.
    layout.pages = 2;
    layout.banks[1] = 2;
    desk_rebuild_model(&m, &map, &console, &layout);
    assert(m.page == 1 && m.bank == 1 && m.page_bank[1] == 1);
    assert(m.page_bank[2] == 0);
    layout.banks[1] = 0;
    desk_rebuild_model(&m, &map, &console, &layout);
    assert(m.bank == 0 && m.page_bank[1] == 0);
    layout.banks[1] = 3;
    desk_set_layout(&m, &layout);
    desk_set_view(&m, 1, 99);
    assert(m.bank == 2 && m.page_bank[1] == 2);
    m.page_bank[1] = 0;
    desk_set_view(&m, 1, 2); // Record even on the no-change fast path.
    assert(m.page_bank[1] == 2);
    desk_set_view(&m, 1, -2);
    assert(m.bank == 0);

    // New layout installation, unlike a console refresh, forgets old banks.
    desk_set_view(&m, 1, 2);
    desk_set_layout(&m, &layout);
    assert(m.page == 0 && m.bank == 0);
    for (int i = 0; i < MAP_MAX_PAGES; i++)
        assert(m.page_bank[i] == 0);
    desk_set_view(&m, 1, -1);
    assert(m.bank == 0);
    puts("desk_bank_memory ok");
    return 0;
}
