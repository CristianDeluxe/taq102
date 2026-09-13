// The settings surface's rectangles: two cards, a footer with the brightness
// fader and the power-aware toggle, and the confirmation sheet. The surface
// covers the rail and the content (0..844); the master column stays.
#ifndef DESK_SETUP_LAYOUT_H
#define DESK_SETUP_LAYOUT_H

#define SETUP_SHEET_X 0
#define SETUP_SHEET_Y 48
#define SETUP_SHEET_W 844
#define SETUP_SHEET_H 552

#define SETUP_HEADER_Y 56
#define SETUP_HEADER_H 48
#define SETUP_CARD_Y 112
#define SETUP_CARD_W 404
#define SETUP_CARD_H 364
#define SETUP_WIFI_X 16
#define SETUP_MASTER_X 428
#define SETUP_CARD_RADIUS 20

#define SETUP_TITLE_H 40        // the card's title row
#define SETUP_CURRENT_H 48      // the line with what is current
#define SETUP_ROW_H 56
#define SETUP_ROWS 3
#define SETUP_ROWS_Y (SETUP_CARD_Y + SETUP_TITLE_H + SETUP_CURRENT_H)   // 152
#define SETUP_BUTTONS_Y (SETUP_CARD_Y + SETUP_CARD_H - 64)              // 412
#define SETUP_BUTTON_H 48
#define SETUP_PAGE_H 48
#define SETUP_PAGE_W 48          // the paging arrows at the right of the title row

#define SETUP_FADER_X 16
#define SETUP_FADER_Y 492
#define SETUP_FADER_W 560
#define SETUP_FADER_H 92
#define SETUP_TOGGLE_X 584
#define SETUP_TOGGLE_Y 492
#define SETUP_TOGGLE_W 260
#define SETUP_TOGGLE_H 92

// The dialog that confirms a join.
#define SETUP_CONFIRM_X 172
#define SETUP_CONFIRM_Y 200
#define SETUP_CONFIRM_W 500
#define SETUP_CONFIRM_H 220

// An explicit way out, on the sheet where the rail would be.
#define SETUP_CLOSE_X 756
#define SETUP_CLOSE_Y 56
#define SETUP_CLOSE_W 88
#define SETUP_CLOSE_H 48

// The gear in the status bar that opens and closes the surface.
#define SETUP_GEAR_X 8
#define SETUP_GEAR_Y 0
#define SETUP_GEAR_W 56
#define SETUP_GEAR_H 48

#endif
