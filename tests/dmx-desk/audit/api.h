#ifndef AUDIT_API_H
#define AUDIT_API_H
#include "context.h"
extern struct audit_context *audit_active;
void audit_note(struct audit_context *a, int warning, const char *rule, const char *fmt, ...);
void audit_record(struct desk_rect rect, const char *label, unsigned char *ink);
void audit_round(struct canvas *c, int x, int y, int w, int h, int r, uint32_t col);
void audit_fill(struct canvas *c, int x, int y, int w, int h, uint32_t col);
void audit_text(struct font *f, struct canvas *c, int x, int y, int max_w, const char *s, uint32_t col);
void audit_glyph_text(struct canvas *c, int x, int y, const char *s, int scale, uint32_t col);
void audit_blend(struct canvas *c, int x, int y, uint32_t col);
void audit_add(struct audit_context *a, int kind, int index, int sub, int enabled,
               const char *label, struct desk_rect drawn, struct desk_rect hit, struct desk_rect allowance);
void audit_inventory(struct audit_context *a);
void audit_setup_inventory(struct audit_context *a);
int audit_reach(struct audit_context *a, int x, int y, int *value);
void audit_geometry(struct audit_context *a);
void audit_pager_selfcheck(struct audit_context *a);
void audit_scan(struct audit_context *a);
void audit_ranges(struct audit_context *a);
void audit_backing(struct audit_context *a, const struct vc_doc *doc);
void audit_view(struct audit_context *a, const char *state);
int audit_selfcheck(struct audit_context *a, const struct vc_doc *doc);
#endif
