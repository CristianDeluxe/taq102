// Fixed chrome of the desk at 1024x600: the rail, the grid and the master
// column. Shared by painting and hit testing so a tile is drawn exactly where
// it is pressed. Map-driven rectangles are resolved elsewhere; these are the
// constants the resolver works inside.
#ifndef DESK_LAYOUT_H
#define DESK_LAYOUT_H

#define DESK_W 1024
#define DESK_H 600

// The status bar owns the top, as it does in every other screen here.
#define DESK_BAR_H 48

#define DESK_RAIL_W 140
#define DESK_MASTER_W 180
#define DESK_MASTER_X (DESK_W - DESK_MASTER_W)   // 844

#define DESK_GAP 16
#define DESK_GRID_X (DESK_RAIL_W + DESK_GAP)     // 156
#define DESK_GRID_Y (DESK_BAR_H + DESK_GAP)      // 64
// Three columns and three rows inside 156..844, gaps of 16 both ways. The
// tile width is what is left, not a number chosen first: three tiles and two
// gaps have to fit 688 px.
#define DESK_COLS 3
#define DESK_ROWS 3
#define DESK_TILE_W ((DESK_MASTER_X - DESK_GRID_X - (DESK_COLS - 1) * DESK_GAP) / DESK_COLS)
#define DESK_TILE_H 160
#define DESK_RADIUS 28

// The master fills from the bottom like the control centre's brightness tile,
// and blackout sits under it.
#define DESK_MASTER_TILE_X (DESK_MASTER_X + DESK_GAP)
#define DESK_MASTER_TILE_Y DESK_GRID_Y
#define DESK_MASTER_TILE_W (DESK_MASTER_W - 2 * DESK_GAP)
#define DESK_MASTER_TILE_H 380
#define DESK_BLACKOUT_Y (DESK_MASTER_TILE_Y + DESK_MASTER_TILE_H + DESK_GAP)
#define DESK_BLACKOUT_H 92

// The appliance palette, unchanged: amber means on, and on this screen the
// show paints it, not the finger.
#define DESK_INK      0xFFF5F5F7u
#define DESK_MUTED    0xFF9A9AA0u
#define DESK_GLASS    0xFF141416u
#define DESK_TILE     0xFF2A2A2Eu
#define DESK_AMBER    0xFFE08A00u
#define DESK_WARN     0xFFC03020u

#endif
