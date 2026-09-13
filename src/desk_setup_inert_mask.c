#include "desk_setup_inert_mask.h"

void desk_setup_inert_mask(const struct desk_setup *s, unsigned char *mask) {
    // Setup owns navigation too. Dialog and keyboard scrims are deliberately
    // inert; the base sheet instead dismisses from outside its cards/footer.
    struct desk_rect sheet = {0, 0, SETUP_SHEET_W, DESK_H};
    desk_region_mark(mask, sheet, s->kb.open || s->confirm_open);
    if (s->kb.open) {
        struct kb_key keys[KB_MAX_KEYS];
        int n = keyboard_keys(&s->kb, keys, KB_MAX_KEYS);
        for (int i = 0; i < n; i++)
            if (keys[i].enabled)
                desk_region_mark(mask, (struct desk_rect){keys[i].x, keys[i].y, keys[i].w, keys[i].h}, 0);
        return;
    }
    if (s->confirm_open) {
        int buttons = s->confirm_known ? 3 : 2;
        int width = (SETUP_CONFIRM_W - 32 - 16 * (buttons - 1)) / buttons;
        for (int i = 0; i < buttons; i++)
            desk_region_mark(mask, (struct desk_rect){SETUP_CONFIRM_X + 16 + i * (width + 16),
                SETUP_CONFIRM_Y + SETUP_CONFIRM_H - 64, width, 48}, 0);
        return;
    }
    // A card's title, status, unused rows and gutters do nothing. Only the
    // declared row/arrow/button envelopes below are reserved for actions.
    for (int card = 0; card < 2; card++) {
        int x = card ? SETUP_MASTER_X : SETUP_WIFI_X;
        int count = card ? s->found_count : s->scan.count;
        int page = card ? s->found_page : s->scan_page;
        desk_region_mark(mask, (struct desk_rect){x, SETUP_CARD_Y, SETUP_CARD_W, SETUP_CARD_H}, 1);
        for (int next = 0; next < 2; next++)
            if (next ? (page + 1) * SETUP_ROWS < count : page > 0)
                desk_region_mark(mask, desk_setup_page_hit(x, next), 0);
        for (int row = 0; row < SETUP_ROWS && page * SETUP_ROWS + row < count; row++)
            if (card || (!s->wifi_busy[0] && wifi_scan_joinable(s->scan.network[page * SETUP_ROWS + row].security)))
                desk_region_mark(mask, desk_setup_row_hit(x, row), 0);
        if (!card && s->wifi_available && !s->wifi_busy[0])
            desk_region_mark(mask, (struct desk_rect){x + 16, SETUP_BUTTONS_Y, SETUP_CARD_W - 32, SETUP_BUTTON_H}, 0);
        if (card) {
            int half = (SETUP_CARD_W - 48) / 2;
            desk_region_mark(mask, (struct desk_rect){x + 16, SETUP_BUTTONS_Y, half, SETUP_BUTTON_H}, 0);
            desk_region_mark(mask, (struct desk_rect){x + 32 + half, SETUP_BUTTONS_Y, half, SETUP_BUTTON_H}, 0);
        }
    }
}
