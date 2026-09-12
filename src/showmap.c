#include "showmap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cjson/cJSON.h>

#include "desk_layout.h"

#define MAP_MAX_BYTES (256 * 1024)

static int string_into(const cJSON *node, const char *name, char *dst,
                       size_t cap, int required) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(node, name);
    if (!cJSON_IsString(item) || !item->valuestring) {
        dst[0] = '\0';
        return required ? -1 : 0;
    }
    if (strlen(item->valuestring) >= cap) {
        fprintf(stderr, "map: %s is longer than %zu bytes\n", name, cap - 1);
        return -1;
    }
    // The wire is pipe-separated, so a label carrying one would be a command
    // injected through a caption.
    if (strchr(item->valuestring, '|')) {
        fprintf(stderr, "map: %s contains the wire's delimiter\n", name);
        return -1;
    }
    strcpy(dst, item->valuestring);
    return 0;
}

static int int_field(const cJSON *node, const char *name, int min, int max,
                     int fallback) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(node, name);
    if (!cJSON_IsNumber(item))
        return fallback;
    double v = cJSON_GetNumberValue(item);
    if (v != (double)(int)v || v < min || v > max)
        return fallback;
    return (int)v;
}

static int parse_action(const cJSON *node, enum map_action *out) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(node, "action");
    if (!cJSON_IsString(item) || !item->valuestring)
        return -1;
    if (strcmp(item->valuestring, "toggle") == 0)
        *out = MAP_TOGGLE;
    else if (strcmp(item->valuestring, "master") == 0)
        *out = MAP_MASTER;
    else if (strcmp(item->valuestring, "blackout") == 0)
        *out = MAP_BLACKOUT;
    else {
        // Flash and momentary are deliberately absent: a held button over a
        // network has no deadline, and this show's fog is one of them.
        fprintf(stderr, "map: unknown action %s\n", item->valuestring);
        return -1;
    }
    return 0;
}

int showmap_parse(const char *json, size_t len, struct show_map *out) {
    if (!json || !out || len == 0 || len > MAP_MAX_BYTES)
        return -1;
    memset(out, 0, sizeof *out);

    char *text = malloc(len + 1);
    if (!text)
        return -1;
    memcpy(text, json, len);
    text[len] = '\0';
    cJSON *root = cJSON_ParseWithOpts(text, NULL, 1);
    free(text);
    if (!root) {
        fprintf(stderr, "map: not JSON, or not the whole file\n");
        return -1;
    }

    int rc = -1;
    if (int_field(root, "schema", 1, 1, -1) != 1) {
        fprintf(stderr, "map: schema is not 1\n");
        goto done;
    }
    const cJSON *show = cJSON_GetObjectItemCaseSensitive(root, "show");
    if (!cJSON_IsObject(show) ||
        string_into(show, "key", out->key, sizeof out->key, 1) != 0 ||
        string_into(show, "workspace", out->workspace, sizeof out->workspace, 1) != 0 ||
        string_into(show, "sha256", out->sha256, sizeof out->sha256, 0) != 0) {
        fprintf(stderr, "map: the show block is incomplete\n");
        goto done;
    }

    const cJSON *controls = cJSON_GetObjectItemCaseSensitive(root, "controls");
    if (!cJSON_IsArray(controls) || cJSON_GetArraySize(controls) < 1) {
        fprintf(stderr, "map: no controls\n");
        goto done;
    }

    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, controls) {
        if (out->count >= MAP_MAX_CONTROLS) {
            fprintf(stderr, "map: more than %d controls\n", MAP_MAX_CONTROLS);
            goto done;
        }
        if (!cJSON_IsObject(item))
            goto done;
        struct map_control *c = &out->control[out->count];
        if (string_into(item, "label", c->label, sizeof c->label, 1) != 0 ||
            parse_action(item, &c->action) != 0)
            goto done;
        c->widget_id = int_field(item, "widget", 0, 0x7FFFFFF, -1);
        c->function_id = int_field(item, "function", 0, 0x7FFFFFF, -1);
        c->row = int_field(item, "row", 0, DESK_ROWS - 1, -1);
        c->col = int_field(item, "col", 0, DESK_COLS - 1, -1);
        if (c->action == MAP_TOGGLE && (c->row < 0 || c->col < 0)) {
            fprintf(stderr, "map: %s has no slot on the grid\n", c->label);
            goto done;
        }
        if (c->action == MAP_TOGGLE && c->widget_id < 0) {
            fprintf(stderr, "map: %s has no widget to press\n", c->label);
            goto done;
        }
        for (int i = 0; i < out->count; i++) {
            const struct map_control *other = &out->control[i];
            if (other->action == MAP_TOGGLE && c->action == MAP_TOGGLE &&
                other->row == c->row && other->col == c->col) {
                fprintf(stderr, "map: %s and %s share a slot\n", other->label,
                        c->label);
                goto done;
            }
        }
        out->count++;
    }
    rc = 0;

done:
    cJSON_Delete(root);
    if (rc != 0)
        memset(out, 0, sizeof *out);
    return rc;
}

int showmap_load(const char *path, struct show_map *out) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "map: cannot open %s\n", path);
        return -1;
    }
    char *buf = malloc(MAP_MAX_BYTES);
    if (!buf) {
        fclose(f);
        return -1;
    }
    size_t len = fread(buf, 1, MAP_MAX_BYTES, f);
    int too_big = !feof(f);
    fclose(f);
    if (too_big) {
        fprintf(stderr, "map: %s is larger than %d bytes\n", path, MAP_MAX_BYTES);
        free(buf);
        return -1;
    }
    int rc = showmap_parse(buf, len, out);
    free(buf);
    return rc;
}
