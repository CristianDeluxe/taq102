// SOURCES: vcjson.c
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vcjson.h"

// The fixture is the real /vc.json of DeluxeEventos.qxw, served by QLC+ 5.2.2
// on 2026-09-12. It is the only document this parser has ever had to read.
static char *read_fixture(size_t *len) {
    const char *path = "tests/dmx-desk/fixtures/vc-deluxe-eventos.json";
    FILE *f = fopen(path, "rb");
    assert(f);
    assert(fseek(f, 0, SEEK_END) == 0);
    long size = ftell(f);
    assert(size > 0);
    assert(fseek(f, 0, SEEK_SET) == 0);
    char *buf = malloc((size_t)size);
    assert(buf);
    assert(fread(buf, 1, (size_t)size, f) == (size_t)size);
    assert(fclose(f) == 0);
    *len = (size_t)size;
    return buf;
}

static void refuses(const char *json) {
    struct vc_doc doc;
    memset(&doc, 0xAA, sizeof doc);
    assert(vc_parse(json, strlen(json), &doc) == -1);
    assert(doc.widget == NULL && doc.count == 0);
}

int main(void) {
    size_t len;
    char *json = read_fixture(&len);

    struct vc_doc doc;
    assert(vc_parse(json, len, &doc) == 0);
    assert(strcmp(doc.app_version, "5.2.2") == 0);
    assert(doc.page_count == 1);
    assert(doc.count == 172);

    // The page is a frame with an id of its own, and that id is not a
    // widget's: the page and the XY pad are both 0. Only the pad is here.
    const struct vc_widget *pad = vc_find(&doc, 0);
    assert(pad && pad->type_id == VC_XYPAD && pad->parent_id == 0);
    assert(strcmp(pad->caption, "Movimiento Cabezas") == 0);

    // The four Flash buttons, the reason the desk never holds a button open.
    const int flash[] = { 112, 113, 121, 125 };
    for (size_t i = 0; i < sizeof flash / sizeof *flash; i++) {
        const struct vc_widget *w = vc_find(&doc, flash[i]);
        assert(w && w->type_id == VC_BUTTON && w->action_type == VC_FLASH);
    }
    const struct vc_widget *fog = vc_find(&doc, 125);
    assert(strcmp(fog->caption, "HUMO") == 0 && fog->function_id == 364);

    // A toggle cue inside its solo frame, and a widget with no function.
    const struct vc_widget *cue = vc_find(&doc, 98);
    assert(cue->type_id == VC_BUTTON && cue->action_type == VC_TOGGLE);
    assert(cue->function_id == 6 && cue->parent_id == 95);
    const struct vc_widget *solo = vc_find(&doc, 95);
    assert(solo->type_id == VC_SOLO_FRAME && solo->function_id == -1);

    // Function id 0 is a real function here and must not read as "none".
    const struct vc_widget *circle = vc_find(&doc, 28);
    assert(circle->function_id == 0 && circle->parent_id == 27);

    const struct vc_widget *speed = vc_find(&doc, 54);
    assert(speed->type_id == VC_SPEED_DIAL);
    assert(strcmp(speed->caption, "Duraci\xc3\xb3n Chasers") == 0);
    assert(speed->speed_ms >= 0 && speed->speed_factor >= 0);

    // Live state, the thing a reconnecting desk has to start from.
    assert(cue->state == 0 && cue->visible == 1 && cue->disabled == 0);
    const struct vc_widget *fader = vc_find(&doc, 130);
    assert(fader->type_id == VC_SLIDER && fader->monitor == 1);
    assert(strcmp(fader->cng_type, "None") == 0);

    // The pad's window is saved zero-wide horizontally. That restricts it to
    // the fine byte rather than pinning it, and the desk must send
    // coordinates inside the window it is told about.
    assert(pad->h_min == 0.0f && pad->h_max == 0.0f);
    assert(pad->v_min == 0.0f && pad->v_max == 256.0f);
    assert(pad->pos_x == 180.0f && pad->pos_y == 185.0f);

    // A document with anything after its closing brace is refused whole, and
    // one larger than a console could plausibly be is refused unread.
    struct vc_doc trailing;
    const char *after = "{\"pages\":[{\"id\":0,\"children\":[{\"id\":1}]}]} junk";
    assert(vc_parse(after, strlen(after), &trailing) == -1);
    assert(vc_parse(json, (size_t)1024 * 1024 + 1, &trailing) == -1);

    assert(vc_find(&doc, 4242) == NULL);
    assert(vc_find(&doc, -1) == NULL);

    // Sorted ascending, no duplicates: the desk addresses widgets by id.
    for (int i = 1; i < doc.count; i++)
        assert(doc.widget[i - 1].id < doc.widget[i].id);

    vc_free(&doc);
    assert(doc.widget == NULL && doc.count == 0);
    vc_free(&doc);                       // idempotent
    vc_free(NULL);

    // A truncated document is refused whole; half a console is not a console.
    struct vc_doc partial;
    assert(vc_parse(json, len / 2, &partial) == -1);
    free(json);

    refuses("");
    refuses("null");
    refuses("[]");
    refuses("{\"pages\":[]}");
    refuses("{\"pages\":{}}");
    refuses("{\"pages\":[{\"id\":0}]}");                  // a page with no widgets
    refuses("{\"pages\":[{\"id\":0,\"children\":[{\"id\":4},{\"id\":4}]}]}");
    refuses("{\"pages\":[{\"id\":0,\"children\":[{\"id\":-4}]}]}");
    refuses("{\"pages\":[{\"id\":0,\"children\":[{\"id\":1.5}]}]}");
    refuses("{\"pages\":[{\"id\":0,\"children\":[{\"id\":\"7\"}]}]}");
    refuses("{\"pages\":[{\"id\":0,\"children\":[7]}]}");

    // A field of the wrong type falls back rather than failing: the desk wants
    // the widgets it can address, and validation catches the rest.
    struct vc_doc odd;
    const char *odd_json =
        "{\"app\":5,\"pages\":[{\"id\":0,\"children\":[{\"id\":3,"
        "\"typeId\":\"x\",\"functionId\":null,\"actionType\":9,"
        "\"caption\":12}]}]}";
    assert(vc_parse(odd_json, strlen(odd_json), &odd) == 0);
    assert(odd.count == 1 && odd.widget[0].type_id == 0);
    assert(odd.widget[0].function_id == -1 && odd.widget[0].action_type == 0);
    assert(odd.widget[0].caption[0] == '\0' && odd.app_version[0] == '\0');
    vc_free(&odd);

    // A caption longer than the field is cut on a character boundary, never
    // mid-sequence: 47 bytes of a 2-byte character leaves 46.
    char big[512];
    int n = snprintf(big, sizeof big,
                     "{\"pages\":[{\"id\":0,\"children\":[{\"id\":1,\"caption\":\"");
    for (int i = 0; i < 40; i++)
        n += snprintf(big + n, sizeof big - n, "\xc3\xb1");
    snprintf(big + n, sizeof big - n, "\"}]}]}");
    struct vc_doc cut;
    assert(vc_parse(big, strlen(big), &cut) == 0);
    assert(strlen(cut.widget[0].caption) == 46);
    assert(strcmp(cut.widget[0].caption + 44, "\xc3\xb1") == 0);
    vc_free(&cut);

    return 0;
}
