#include "api.h"
#include <stdlib.h>
#include <string.h>
void audit_record(struct desk_rect rect, const char *label, unsigned char *ink) {
    struct audit_context *a = audit_active;
    if (!a || rect.w <= 0 || rect.h <= 0) { free(ink); return; }
    if (a->draws == AUDIT_MAX_DRAWS) {
        fprintf(stderr, "audit draw capacity exhausted; coverage incomplete\n");
        exit(2);
    }
    struct audit_draw *d = &a->draw[a->draws++];
    *d = (struct audit_draw){ .rect = rect, .text = label != NULL, .ink = ink };
    snprintf(d->label, sizeof d->label, "%s", label ? label : "paint primitive");
}
