// The SPEED page's rectangles, under the compact state row: two cards of
// 688x160 and the Tap both strip of 688x96, which together fill the 448 px
// left exactly. Inside a card: the readout, a large Tap, and a grid of six
// 80x60 targets (-1 BPM, +1 BPM, x1 / x1/2, x2).
#ifndef DESK_SPEED_LAYOUT_H
#define DESK_SPEED_LAYOUT_H

#define SPEED_CARD_X 16
#define SPEED_CARD_W 828
#define SPEED_CARD_H 160
#define SPEED_CARD_Y(i) (56 + (i) * 176)      // 56, 232
#define SPEED_BOTH_Y 408
#define SPEED_BOTH_H 64
#define SPEED_RADIUS 20

#define SPEED_READOUT_X 16
#define SPEED_READOUT_W 208
#define SPEED_TAP_X 236
#define SPEED_TAP_Y 16
#define SPEED_TAP_W 176
#define SPEED_TAP_H 128
#define SPEED_GRID_X 424
#define SPEED_GRID_Y 16
#define SPEED_CELL_W 80
#define SPEED_CELL_H 60
#define SPEED_CELL_GAP 8
#define SPEED_CELL_X(col) (SPEED_GRID_X + (col) * (SPEED_CELL_W + SPEED_CELL_GAP))
#define SPEED_CELL_Y(row) (SPEED_GRID_Y + (row) * (SPEED_CELL_H + SPEED_CELL_GAP))

#endif
