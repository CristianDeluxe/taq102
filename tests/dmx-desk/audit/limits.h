#ifndef AUDIT_LIMITS_H
#define AUDIT_LIMITS_H

// Owner-supplied panel dimensions and 9 mm finger-target guidance. Round up
// each axis independently: ceil(9*1024/223)=42, ceil(9*600/125)=44.
// Area alone would bless a long, unusably thin strip; require both spans too.
#define AUDIT_PANEL_WIDTH_MM 223
#define AUDIT_PANEL_HEIGHT_MM 125
#define AUDIT_TARGET_MM 9
#define AUDIT_MIN_WIDTH ((AUDIT_TARGET_MM * DESK_W + AUDIT_PANEL_WIDTH_MM - 1) / AUDIT_PANEL_WIDTH_MM)
#define AUDIT_MIN_HEIGHT ((AUDIT_TARGET_MM * DESK_H + AUDIT_PANEL_HEIGHT_MM - 1) / AUDIT_PANEL_HEIGHT_MM)
#define AUDIT_MIN_AREA (AUDIT_MIN_WIDTH * AUDIT_MIN_HEIGHT)
// One finger-sized patch of inert glass is worth investigating. Edge-connected
// components fail regardless of area. Only the production inert-region contract
// exempts background/gutters; conflicting enabled paint/actions fail separately.
#define AUDIT_MAX_DEAD_AREA AUDIT_MIN_AREA
// Integer interpolation may round the midpoint by one output unit.
#define AUDIT_RANGE_TOLERANCE 1
// Probe every screen pixel, plus a one-pixel exterior ring. Rectangular hit
// candidates are also checked analytically, including any farther overhang.
#define AUDIT_PIXELS (DESK_W * DESK_H)
#define AUDIT_MAX_ELEMENTS 512
#define AUDIT_MAX_DRAWS 4096

#endif
