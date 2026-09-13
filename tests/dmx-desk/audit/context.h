#ifndef AUDIT_CONTEXT_H
#define AUDIT_CONTEXT_H
#include <stdio.h>
#include "desk_layout.h"
#include "desk_model.h"
#include "desk_fonts.h"
#include "desk_hold.h"
#include "desk_speed.h"
#include "desk_setup.h"
#include "element.h"
#include "draw.h"
#include "limits.h"
struct audit_context {
    struct desk_model model;
    struct desk_hold hold;
    struct desk_speed speed;
    struct desk_setup setup;
    struct desk_fonts fonts;
    struct canvas canvas;
    struct audit_element element[AUDIT_MAX_ELEMENTS];
    int elements;
    struct audit_draw draw[AUDIT_MAX_DRAWS];
    int draws;
    char view[160];
    int failures, warnings, views;
    int inject_pager_dead_strip;
    long probes;
    FILE *report;
};
#endif
