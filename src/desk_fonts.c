#include "desk_fonts.h"

#include <stdio.h>
#include <stdlib.h>

int desk_fonts_open(struct desk_fonts *f, const char *dir) {
    char sb[512], rg[512];
    snprintf(sb, sizeof sb, "%s/Inter-SemiBold.ttf", dir);
    snprintf(rg, sizeof rg, "%s/Inter-Regular.ttf", dir);
    f->tile = font_open(sb, 17);
    f->tile_s = font_open(sb, 14);
    f->value = font_open(sb, 30);
    f->big = font_open(sb, 44);
    f->tab = font_open(sb, 15);
    f->section = font_open(sb, 12);
    f->label = font_open(rg, 15);
    f->small = font_open(rg, 13);
    return f->tile && f->label ? 0 : -1;
}

void desk_fonts_close(struct desk_fonts *f) {
    struct font **faces[] = { &f->tile, &f->tile_s, &f->value, &f->big, &f->tab, &f->section,
                              &f->label, &f->small };
    for (size_t i = 0; i < sizeof faces / sizeof faces[0]; i++) {
        font_close(*faces[i]);
        *faces[i] = NULL;
    }
}

