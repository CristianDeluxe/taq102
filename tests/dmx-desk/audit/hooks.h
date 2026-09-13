// Only the host painter translation units include this. Production code and
// its drawing/routing behavior are unchanged; the real primitives still run.
#include "api.h"
#define canvas_round_rect audit_round
#define canvas_fill_rect audit_fill
#define font_draw_fit audit_text
#define canvas_text audit_glyph_text
