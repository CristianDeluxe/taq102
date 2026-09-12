#include "showmap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cjson/cJSON.h>

#define MAP_MAX_BYTES (512 * 1024)

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
    // The wire is pipe-separated, so a caption carrying one would be a
    // command injected through a label.
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

static int bool_field(const cJSON *node, const char *name, int fallback) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(node, name);
    if (cJSON_IsTrue(item))
        return 1;
    if (cJSON_IsFalse(item))
        return 0;
    return fallback;
}

static int parse_role(const char *word, enum map_role *out) {
    static const struct { const char *word; enum map_role role; } table[] = {
        { "state", MAP_ROLE_STATE }, { "accent", MAP_ROLE_ACCENT },
        { "haze", MAP_ROLE_HAZE },   { "hook", MAP_ROLE_HOOK },
        { "pick", MAP_ROLE_PICK },   { "chase", MAP_ROLE_CHASE },
        { "toggle", MAP_ROLE_TOGGLE },
    };
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++) {
        if (strcmp(word, table[i].word) == 0) {
            *out = table[i].role;
            return 0;
        }
    }
    return -1;
}

static int parse_swatch(const char *text, uint32_t *out) {
    if (strlen(text) != 7 || text[0] != '#')
        return -1;
    uint32_t v = 0;
    for (int i = 1; i < 7; i++) {
        char ch = text[i];
        int digit = ch >= '0' && ch <= '9' ? ch - '0'
                  : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10
                  : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
        if (digit < 0)
            return -1;
        v = (v << 4) | (uint32_t)digit;
    }
    *out = v;
    return 0;
}

// One control's own fields; its page, section and position come from the
// section that lists it.
static int parse_control(const char *key, const cJSON *item, struct map_control *c) {
    memset(c, 0, sizeof *c);
    if (strlen(key) >= sizeof c->key || strchr(key, '|')) {
        fprintf(stderr, "map: control key %.20s is not usable\n", key);
        return -1;
    }
    strcpy(c->key, key);
    if (!cJSON_IsObject(item) ||
        string_into(item, "caption", c->caption, sizeof c->caption, 1) != 0 ||
        string_into(item, "detail", c->detail, sizeof c->detail, 0) != 0 ||
        string_into(item, "reason", c->reason, sizeof c->reason, 0) != 0) {
        fprintf(stderr, "map: control %s is incomplete\n", key);
        return -1;
    }
    char word[16];
    if (string_into(item, "role", word, sizeof word, 1) != 0 || parse_role(word, &c->role) != 0) {
        fprintf(stderr, "map: control %s has an unknown role\n", key);
        return -1;
    }
    if (string_into(item, "action", word, sizeof word, 1) != 0)
        return -1;
    if (strcmp(word, "toggle") == 0)
        c->held = 0;
    else if (strcmp(word, "flash") == 0)
        c->held = 1;
    else {
        fprintf(stderr, "map: control %s has an unknown action %s\n", key, word);
        return -1;
    }
    c->widget_id = int_field(item, "widget", 0, 0x7FFFFFF, -1);
    if (c->widget_id < 0) {
        fprintf(stderr, "map: control %s has no widget\n", key);
        return -1;
    }
    c->function_id = int_field(item, "function", 0, 0x7FFFFFF, -1);
    c->solo_id = int_field(item, "solo", 0, 0x7FFFFFF, -1);
    c->enabled = bool_field(item, "enabled", 0);
    // A held button is never enabled here whatever the generator said: the
    // desk holds nothing open across a network.
    if (c->held)
        c->enabled = 0;
    const cJSON *swatches = cJSON_GetObjectItemCaseSensitive(item, "swatches");
    const cJSON *s = NULL;
    cJSON_ArrayForEach(s, swatches) {
        if (c->swatches >= MAP_MAX_SWATCHES)
            break;
        if (!cJSON_IsString(s) || parse_swatch(s->valuestring, &c->swatch[c->swatches]) != 0) {
            fprintf(stderr, "map: control %s has a swatch that is not #rrggbb\n", key);
            return -1;
        }
        c->swatches++;
    }
    return 0;
}

// The multiplier enum as the map writes it: `{"raw": n, ...}`; the name and
// factor beside it are for readers, the raw value is what the engine holds.
static int parse_multiplier(const cJSON *node, const char *name, int *out) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(node, name);
    if (!cJSON_IsObject(item))
        return -1;
    *out = int_field(item, "raw", 0, 10, -1);
    return *out < 0 ? -1 : 0;
}

static int parse_dials(const cJSON *dials, struct show_map *out) {
    out->dials = 0;
    if (!dials || cJSON_IsNull(dials))
        return 0;
    if (!cJSON_IsObject(dials)) {
        fprintf(stderr, "map: dials is not an object\n");
        return -1;
    }
    const cJSON *item;
    cJSON_ArrayForEach(item, dials) {
        if (out->dials >= MAP_MAX_DIALS) {
            fprintf(stderr, "map: more than %d dials\n", MAP_MAX_DIALS);
            return -1;
        }
        struct map_dial *d = &out->dial[out->dials];
        memset(d, 0, sizeof *d);
        if (!item->string || strlen(item->string) >= sizeof d->key || !cJSON_IsObject(item)) {
            fprintf(stderr, "map: dial key %.20s is not usable\n", item->string ? item->string : "");
            return -1;
        }
        snprintf(d->key, sizeof d->key, "%s", item->string);
        if (string_into(item, "caption", d->caption, sizeof d->caption, 1) != 0)
            return -1;
        d->widget_id = int_field(item, "widget", 0, 0x7FFFFFF, -1);
        d->time_ms = int_field(item, "timeMs", 0, 600000, -1);
        if (d->widget_id < 0 || d->time_ms < 0) {
            fprintf(stderr, "map: dial %s lacks its widget or time\n", d->key);
            return -1;
        }
        const cJSON *members = cJSON_GetObjectItemCaseSensitive(item, "members");
        if (!cJSON_IsArray(members)) {
            fprintf(stderr, "map: dial %s has no members\n", d->key);
            return -1;
        }
        const cJSON *m;
        cJSON_ArrayForEach(m, members) {
            if (d->members >= MAP_MAX_DIAL_MEMBERS) {
                fprintf(stderr, "map: dial %s has more than %d members\n", d->key, MAP_MAX_DIAL_MEMBERS);
                return -1;
            }
            struct map_dial_member *mm = &d->member[d->members];
            mm->function_id = int_field(m, "function", 0, 0x7FFFFFF, -1);
            if (mm->function_id < 0 ||
                parse_multiplier(m, "fadeIn", &mm->fade_in) != 0 ||
                parse_multiplier(m, "fadeOut", &mm->fade_out) != 0 ||
                parse_multiplier(m, "duration", &mm->duration) != 0) {
                fprintf(stderr, "map: dial %s member %d is malformed\n", d->key, d->members);
                return -1;
            }
            d->members++;
        }
        out->dials++;
    }
    return 0;
}

static int parse_pages(const cJSON *pages, const cJSON *controls, struct show_map *out) {
    // Every control parsed once, then placed where a section lists it.
    struct map_control *pool = calloc(MAP_MAX_CONTROLS, sizeof *pool);
    int pooled = 0;
    if (!pool)
        return -1;
    int rc = -1;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, controls) {
        if (pooled >= MAP_MAX_CONTROLS) {
            fprintf(stderr, "map: more than %d controls\n", MAP_MAX_CONTROLS);
            goto done;
        }
        if (parse_control(item->string ? item->string : "", item, &pool[pooled]) != 0)
            goto done;
        for (int i = 0; i < pooled; i++) {
            if (pool[i].widget_id == pool[pooled].widget_id) {
                fprintf(stderr, "map: %s and %s share widget %d\n", pool[i].key,
                        pool[pooled].key, pool[pooled].widget_id);
                goto done;
            }
        }
        pooled++;
    }
    if (pooled == 0) {
        fprintf(stderr, "map: no controls\n");
        goto done;
    }

    const cJSON *page = NULL;
    cJSON_ArrayForEach(page, pages) {
        if (out->pages >= MAP_MAX_PAGES) {
            fprintf(stderr, "map: more than %d pages\n", MAP_MAX_PAGES);
            goto done;
        }
        struct map_page *p = &out->page[out->pages];
        if (!cJSON_IsObject(page) ||
            string_into(page, "key", p->key, sizeof p->key, 1) != 0 ||
            string_into(page, "title", p->title, sizeof p->title, 1) != 0)
            goto done;
        const cJSON *sections = cJSON_GetObjectItemCaseSensitive(page, "sections");
        const cJSON *section = NULL;
        cJSON_ArrayForEach(section, sections) {
            if (p->sections >= MAP_MAX_SECTIONS) {
                fprintf(stderr, "map: page %s has more than %d sections\n", p->key, MAP_MAX_SECTIONS);
                goto done;
            }
            struct map_section *sec = &p->section[p->sections];
            if (!cJSON_IsObject(section) ||
                string_into(section, "key", sec->key, sizeof sec->key, 1) != 0 ||
                string_into(section, "title", sec->title, sizeof sec->title, 1) != 0)
                goto done;
            sec->solo_id = int_field(section, "solo", 0, 0x7FFFFFF, -1);
            sec->first = out->count;
            const cJSON *keys = cJSON_GetObjectItemCaseSensitive(section, "controls");
            const cJSON *k = NULL;
            cJSON_ArrayForEach(k, keys) {
                if (!cJSON_IsString(k) || !k->valuestring)
                    goto done;
                int found = -1;
                for (int i = 0; i < pooled; i++) {
                    if (strcmp(pool[i].key, k->valuestring) == 0) {
                        found = i;
                        break;
                    }
                }
                if (found < 0) {
                    fprintf(stderr, "map: section %s/%s lists %s, which is not a control\n",
                            p->key, sec->key, k->valuestring);
                    goto done;
                }
                if (pool[found].page >= 0 && pool[found].section >= 0 && pool[found].caption[0] == '\1') {
                    goto done;
                }
                if (out->count >= MAP_MAX_CONTROLS)
                    goto done;
                struct map_control *c = &out->control[out->count];
                *c = pool[found];
                c->page = out->pages;
                c->section = p->sections;
                // Mark the pooled copy as placed by blanking its key, so a
                // control listed twice is caught.
                pool[found].key[0] = '\0';
                out->count++;
                sec->count++;
            }
            p->sections++;
        }
        out->pages++;
    }
    for (int i = 0; i < pooled; i++) {
        if (pool[i].key[0]) {
            fprintf(stderr, "map: control %s is listed by no section\n", pool[i].key);
            goto done;
        }
    }
    if (out->count != pooled) {
        fprintf(stderr, "map: a control is listed twice\n");
        goto done;
    }
    rc = 0;
done:
    free(pool);
    return rc;
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
    out->schema = int_field(root, "schema", 2, 2, -1);
    if (out->schema != 2) {
        fprintf(stderr, "map: schema is not 2\n");
        goto done;
    }
    if (string_into(root, "qlcVersion", out->qlc_version, sizeof out->qlc_version, 1) != 0)
        goto done;
    const cJSON *show = cJSON_GetObjectItemCaseSensitive(root, "show");
    if (!cJSON_IsObject(show) ||
        string_into(show, "key", out->key, sizeof out->key, 1) != 0 ||
        string_into(show, "workspace", out->workspace, sizeof out->workspace, 1) != 0 ||
        string_into(show, "sha256", out->sha256, sizeof out->sha256, 0) != 0) {
        fprintf(stderr, "map: the show block is incomplete\n");
        goto done;
    }
    const cJSON *gm = cJSON_GetObjectItemCaseSensitive(root, "grandMaster");
    out->grand_master_widget = cJSON_IsObject(gm) ? int_field(gm, "widget", 0, 0x7FFFFFF, -1) : -1;
    const cJSON *stop = cJSON_GetObjectItemCaseSensitive(root, "stopAll");
    out->stop_all_widget = cJSON_IsObject(stop) ? int_field(stop, "widget", 0, 0x7FFFFFF, -1) : -1;
    out->stop_all_fade_ms = cJSON_IsObject(stop) ? int_field(stop, "fadeOutMs", 0, 60000, 0) : 0;

    const cJSON *pages = cJSON_GetObjectItemCaseSensitive(root, "pages");
    const cJSON *controls = cJSON_GetObjectItemCaseSensitive(root, "controls");
    if (!cJSON_IsArray(pages) || !cJSON_IsObject(controls)) {
        fprintf(stderr, "map: pages or controls missing\n");
        goto done;
    }
    if (parse_pages(pages, controls, out) != 0)
        goto done;
    if (parse_dials(cJSON_GetObjectItemCaseSensitive(root, "dials"), out) != 0)
        goto done;
    if (out->pages == 0 || out->count == 0) {
        fprintf(stderr, "map: nothing to show\n");
        goto done;
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
