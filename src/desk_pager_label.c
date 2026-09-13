#include "desk_pager_label.h"

const char *desk_pager_label(const struct desk_layout *layout, int page, int bank) {
    for (int i = 0; i < layout->headings; i++) {
        const struct desk_heading *h = &layout->heading[i];
        if (h->page == page && h->bank == bank)
            return h->text;
    }
    return "";
}
