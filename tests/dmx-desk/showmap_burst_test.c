// SOURCES: showmap.c showmap_validate.c desk_model.c desk_master_level_at.c vcjson.c
// A burst in the map: role, length and the hit it stands for parsed; a
// burst without a length or a function refused; against a console with a
// toggle over the chaser it validates as a burst control the master's own
// FUNCTION push lights.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "desk_model.h"
#include "showmap.h"
#include "showmap_validate.h"
#include "vcjson.h"

static const char MAP[] =
    "{\"schema\":2,\"qlcVersion\":\"5.2.2\",\"show\":{\"key\":\"t\",\"workspace\":\"t.qxw\",\"sha256\":\"\"},"
    "\"grandMaster\":null,\"stopAll\":null,"
    "\"pages\":[{\"key\":\"live\",\"title\":\"LIVE\",\"sections\":[{\"key\":\"accents\",\"title\":\"GOLPES\",\"solo\":null,\"controls\":[\"humo-ya\"]}]}],"
    "\"controls\":{\"humo-ya\":{\"widget\":300,\"function\":813,\"functionType\":\"Chaser\",\"action\":\"toggle\","
    "\"caption\":\"HUMO YA\",\"detail\":\"\",\"role\":\"burst\",\"burstMs\":3000,\"source\":\"humo-ya\",\"solo\":null,\"key\":null,"
    "\"enabled\":true,\"reason\":\"\",\"swatches\":[]}}}";

static const char VC[] =
    "{\"app\":{\"version\":\"5.2.2\"},\"pages\":[{\"id\":0,\"children\":["
    "{\"id\":300,\"typeId\":1,\"caption\":\"Desk humo\",\"functionId\":813,\"actionType\":0,\"state\":0,\"visible\":false,\"disabled\":false}"
    "]}]}";

int main(void) {
    struct show_map map;
    assert(showmap_parse(MAP, strlen(MAP), &map) == 0);
    assert(map.count == 1 && map.control[0].role == MAP_ROLE_BURST);
    assert(map.control[0].burst_ms == 3000 && strcmp(map.control[0].source, "humo-ya") == 0);
    assert(map.control[0].function_id == 813 && !map.control[0].held);

    // Without a length it is not a burst.
    char bad[sizeof MAP + 8];
    snprintf(bad, sizeof bad, "%s", MAP);
    char *at = strstr(bad, "\"burstMs\":3000");
    memcpy(at, "\"burstMs\":0000", 14);
    struct show_map refused;
    assert(showmap_parse(bad, strlen(bad), &refused) == -1);

    struct vc_doc console;
    assert(vc_parse(VC, strlen(VC), &console) == 0);
    struct desk_model model;
    int enabled = showmap_build(&model, &map, &console);
    assert(enabled >= 1);
    const struct desk_control *b = &model.control[0];
    assert(b->kind == DESK_BURST && b->enabled && b->burst_ms == 3000 && b->function_id == 813);
    assert(b->state == DESK_OFF);
    // The master's word lights it and puts it out.
    desk_apply_function(&model, 813, 1);
    assert(model.control[0].state == DESK_ON);
    desk_apply_function(&model, 813, 0);
    assert(model.control[0].state == DESK_OFF);
    vc_free(&console);
    printf("showmap burst ok\n");
    return 0;
}
