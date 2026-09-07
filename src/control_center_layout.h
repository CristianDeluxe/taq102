#ifndef CONTROL_CENTER_LAYOUT_H
#define CONTROL_CENTER_LAYOUT_H

struct cc_rect { int x, y, w, h; };
/* Absolute screen coordinates; the painter subtracts CC_PANEL's origin. */
#define CC_PANEL ((struct cc_rect){416, 56, 560, 400})
#define CC_HANDLE ((struct cc_rect){678, 66, 36, 5})
#define CC_WIFI ((struct cc_rect){432, 80, 264, 160})
#define CC_BRIGHT ((struct cc_rect){712, 80, 120, 328})
#define CC_AUTO ((struct cc_rect){848, 80, 112, 76})
#define CC_OFFNOW ((struct cc_rect){848, 164, 112, 76})
#define CC_RESCUE ((struct cc_rect){848, 248, 112, 76})
#define CC_LOADER ((struct cc_rect){848, 332, 112, 76})
#define CC_TIMER ((struct cc_rect){432, 256, 264, 76})
#define CC_TIMER_SEGMENT(i) ((struct cc_rect){432 + (i) * 66, 289, 66, 43})
#define CC_TIMER_SELECTION(i) ((struct cc_rect){436 + (i) * 66, 290, 58, 36})
#define CC_WIFI_TITLE ((struct cc_rect){448, 88, 208, 30})
#define CC_WIFI_NAME ((struct cc_rect){448, 124, 232, 34})
#define CC_WIFI_ADDRESS ((struct cc_rect){448, 166, 232, 26})
#define CC_WIFI_SIGNAL ((struct cc_rect){448, 202, 232, 26})
#define CC_WIFI_DOT ((struct cc_rect){668, 99, 12, 12})
#define CC_BRIGHT_FILL ((struct cc_rect){720, 88, 104, 312})
#define CC_BRIGHT_LABEL ((struct cc_rect){720, 92, 104, 28})
#define CC_BRIGHT_VALUE ((struct cc_rect){720, 204, 104, 66})
#define CC_BRIGHT_PERCENT ((struct cc_rect){720, 274, 104, 28})
#define CC_AUTO_LABEL ((struct cc_rect){860, 104, 64, 28})
#define CC_AUTO_DOT ((struct cc_rect){932, 112, 12, 12})
#define CC_OFF_LABEL ((struct cc_rect){856, 175, 96, 28})
#define CC_OFF_NOW ((struct cc_rect){856, 201, 96, 28})
#define CC_ACTION_LABEL(tile) ((struct cc_rect){(tile).x + 8, (tile).y + 24, (tile).w - 16, 28})
#define CC_ARMED_LABEL(tile) ((struct cc_rect){(tile).x + 8, (tile).y + 9, (tile).w - 16, 28})
#define CC_ARMED_HINT(tile) ((struct cc_rect){(tile).x + 8, (tile).y + 39, (tile).w - 16, 28})
#define CC_TIMER_TITLE ((struct cc_rect){448, 262, 232, 26})
#define CC_NOTE ((struct cc_rect){432, 354, 264, 28})
#define CC_FOOTER ((struct cc_rect){432, 416, 528, 24})
#define CC_FOOTER_BASELINE 436
#define CC_CARD_RADIUS 28
#define CC_TILE_RADIUS 20
#define CC_FILL_RADIUS 12
#define CC_SEGMENT_RADIUS 12
#define CC_DOT_RADIUS 6
#define CC_HANDLE_RADIUS 2
#define CC_SLIDE_DISTANCE 408.f
#endif
