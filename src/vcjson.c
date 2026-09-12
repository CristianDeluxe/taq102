#include "vcjson.h"

#include <stdlib.h>
#include <string.h>

#include <cjson/cJSON.h>

// A console with more widgets than this is not this show and not a mistake
// worth allocating for: DeluxeEventos has 172.
#define VC_MAX_WIDGETS 4096
// The real document is 71 KB. A megabyte is already not a console, and the
// parser allocates a copy of whatever it is given.
#define VC_MAX_BYTES (1024 * 1024)

struct build {
    struct vc_widget *widget;
    int count;
};

static int number_field(const cJSON *node, const char *name, int min, int max,
                        int fallback) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(node, name);
    if (!cJSON_IsNumber(item))
        return fallback;
    double v = cJSON_GetNumberValue(item);
    if (!(v >= min && v <= max) || v != (double)(int)v)
        return fallback;
    return (int)v;
}

static int bool_field(const cJSON *node, const char *name, int fallback) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(node, name);
    if (cJSON_IsBool(item))
        return cJSON_IsTrue(item) ? 1 : 0;
    return fallback;
}

static float number_or(const cJSON *node, const char *name, float fallback) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(node, name);
    if (!cJSON_IsNumber(item))
        return fallback;
    double v = cJSON_GetNumberValue(item);
    if (!(v >= -1e9 && v <= 1e9))
        return fallback;
    return (float)v;
}

// A {"min":a,"max":b} pair, left alone when the document does not carry one.
static void range_field(const cJSON *node, const char *name, float *min,
                        float *max) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(node, name);
    if (!cJSON_IsObject(item))
        return;
    *min = number_or(item, "min", *min);
    *max = number_or(item, "max", *max);
}

static void string_field(const cJSON *node, const char *name, char *dst,
                         size_t cap) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(node, name);
    dst[0] = '\0';
    if (!cJSON_IsString(item) || !item->valuestring)
        return;
    strncpy(dst, item->valuestring, cap - 1);
    dst[cap - 1] = '\0';
}

static void copy_caption(const cJSON *node, char *dst) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(node, "caption");
    dst[0] = '\0';
    if (!cJSON_IsString(item) || !item->valuestring)
        return;
    // UTF-8 in, UTF-8 out, cut on a byte boundary that is not mid-sequence.
    size_t n = strlen(item->valuestring);
    if (n > VC_CAPTION_MAX - 1) {
        n = VC_CAPTION_MAX - 1;
        while (n > 0 && (item->valuestring[n] & 0xC0) == 0x80)
            n--;
    }
    memcpy(dst, item->valuestring, n);
    dst[n] = '\0';
}

// Returns -1 if the tree is unusable: a node without an id, or more nodes than
// a console can plausibly hold.
static int collect(const cJSON *node, int parent_id, int page,
                   struct build *out) {
    if (!cJSON_IsObject(node))
        return -1;
    if (out->count >= VC_MAX_WIDGETS)
        return -1;

    int id = number_field(node, "id", 0, 0x7FFFFFF, -1);
    if (id < 0)
        return -1;

    struct vc_widget *w = &out->widget[out->count++];
    memset(w, 0, sizeof *w);
    w->id = id;
    w->parent_id = parent_id;
    w->type_id = number_field(node, "typeId", 0, 255, 0);
    w->function_id = number_field(node, "functionId", 0, 0x7FFFFFF, -1);
    w->action_type = number_field(node, "actionType", 0, 3, 0);
    w->page = number_field(node, "page", 0, 255, page);
    copy_caption(node, w->caption);

    w->visible = bool_field(node, "visible", 1);
    w->disabled = bool_field(node, "disabled", 0);
    w->state = number_field(node, "state", 0, 255, 0);
    w->value = number_field(node, "value", 0, 255, 0);
    w->monitor = bool_field(node, "monitor", 0);
    w->overriding = bool_field(node, "isOverriding", 0);
    string_field(node, "clickAndGoType", w->cng_type, sizeof w->cng_type);
    w->speed_ms = number_field(node, "currentTime", 0, 0x7FFFFFF, -1);
    w->speed_factor = number_field(node, "currentFactor", 0, 255, -1);
    w->speed_min_ms = number_field(node, "timeMin", 0, 0x7FFFFFF, -1);
    w->speed_max_ms = number_field(node, "timeMax", 0, 0x7FFFFFF, -1);

    // A pad with no window in the document may travel the whole universe;
    // one with a window may not, and the desk has to know which before it
    // sends a coordinate.
    w->h_min = w->v_min = 0.0f;
    w->h_max = w->v_max = 255.0f;
    range_field(node, "horizontalRange", &w->h_min, &w->h_max);
    range_field(node, "verticalRange", &w->v_min, &w->v_max);
    const cJSON *position = cJSON_GetObjectItemCaseSensitive(node, "position");
    if (cJSON_IsObject(position)) {
        w->pos_x = number_or(position, "x", 0.0f);
        w->pos_y = number_or(position, "y", 0.0f);
    }

    const cJSON *children = cJSON_GetObjectItemCaseSensitive(node, "children");
    if (cJSON_IsArray(children)) {
        const cJSON *child = NULL;
        cJSON_ArrayForEach(child, children) {
            if (collect(child, id, w->page, out) != 0)
                return -1;
        }
    }
    return 0;
}

static int by_id(const void *a, const void *b) {
    int x = ((const struct vc_widget *)a)->id;
    int y = ((const struct vc_widget *)b)->id;
    return (x > y) - (x < y);
}

int vc_parse(const char *json, size_t len, struct vc_doc *out) {
    if (!json || !out || len > VC_MAX_BYTES)
        return -1;
    memset(out, 0, sizeof *out);

    // cJSON_ParseWithLength wants the terminator inside the length it is
    // given, and an HTTP body does not come with one, so the document is
    // copied once and terminated here. 71 KB for this show, freed before the
    // desk draws its first frame.
    char *text = malloc(len + 1);
    if (!text)
        return -1;
    memcpy(text, json, len);
    text[len] = '\0';
    // require_nul_terminated: a document with anything after its closing brace
    // is refused rather than half read.
    cJSON *root = cJSON_ParseWithOpts(text, NULL, 1);
    free(text);
    if (!root)
        return -1;

    int rc = -1;
    struct build build = { NULL, 0 };
    const cJSON *pages = cJSON_GetObjectItemCaseSensitive(root, "pages");
    if (!cJSON_IsArray(pages) || cJSON_GetArraySize(pages) < 1)
        goto done;

    build.widget = calloc(VC_MAX_WIDGETS, sizeof *build.widget);
    if (!build.widget)
        goto done;

    // A page is a frame with its own id, and that id is not in the widgets'
    // namespace: this show's single page and its XY pad are both id 0. Pages
    // are not addressable, so only their contents are collected.
    const cJSON *page = NULL;
    int page_index = 0;
    cJSON_ArrayForEach(page, pages) {
        if (!cJSON_IsObject(page))
            goto done;
        int page_id = number_field(page, "id", 0, 0x7FFFFFF, -1);
        const cJSON *children = cJSON_GetObjectItemCaseSensitive(page, "children");
        if (cJSON_IsArray(children)) {
            const cJSON *child = NULL;
            cJSON_ArrayForEach(child, children) {
                if (collect(child, page_id, page_index, &build) != 0)
                    goto done;
            }
        }
        page_index++;
    }
    if (build.count == 0)
        goto done;

    qsort(build.widget, build.count, sizeof *build.widget, by_id);
    for (int i = 1; i < build.count; i++) {
        if (build.widget[i].id == build.widget[i - 1].id)
            goto done;      // two widgets on one id: the desk cannot address it
    }

    struct vc_widget *shrunk = realloc(build.widget,
                                       (size_t)build.count * sizeof *shrunk);
    if (shrunk)
        build.widget = shrunk;

    const cJSON *app = cJSON_GetObjectItemCaseSensitive(root, "app");
    const cJSON *version = cJSON_IsObject(app)
        ? cJSON_GetObjectItemCaseSensitive(app, "version") : NULL;
    if (cJSON_IsString(version) && version->valuestring) {
        strncpy(out->app_version, version->valuestring,
                sizeof out->app_version - 1);
    }
    out->page_count = cJSON_GetArraySize(pages);
    out->count = build.count;
    out->widget = build.widget;
    build.widget = NULL;
    rc = 0;

done:
    free(build.widget);
    cJSON_Delete(root);
    if (rc != 0)
        memset(out, 0, sizeof *out);
    return rc;
}

void vc_free(struct vc_doc *doc) {
    if (!doc)
        return;
    free(doc->widget);
    memset(doc, 0, sizeof *doc);
}

const struct vc_widget *vc_find(const struct vc_doc *doc, int id) {
    if (!doc || !doc->widget)
        return NULL;
    int lo = 0, hi = doc->count - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (doc->widget[mid].id == id)
            return &doc->widget[mid];
        if (doc->widget[mid].id < id)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}
