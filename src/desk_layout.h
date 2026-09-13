// Fixed chrome of the desk at 1024x600, second design: a bar of tabs and
// status across the top, the content under it, the master column at the
// right. Shared by painting and hit testing so a tile is drawn exactly where
// it is pressed. Everything sits on an 8 px spacing system; the exceptions
// (tab gap 4, segment gap 4) are named where they are used.
#ifndef DESK_LAYOUT_H
#define DESK_LAYOUT_H

#define DESK_W 1024
#define DESK_H 600

// The bar: tabs from the left, the status cluster and the two targets at
// the right. Gear and lock are 48x48 targets around 24 px icons.
#define DESK_BAR_H 48
#define DESK_TAB_X 16
#define DESK_TAB_Y 8
#define DESK_TAB_W 96
#define DESK_TAB_H 32
#define DESK_TAB_GAP 4
#define DESK_GEAR_X 912
#define DESK_LOCK_X 960
#define DESK_TARGET 48

// The content and the master column.
#define DESK_CONTENT_X 16
#define DESK_CONTENT_W 828
#define DESK_CONTENT_Y 56
#define DESK_CONTENT_END 584
#define DESK_MASTER_X 860
#define DESK_MASTER_W 148
#define DESK_MASTER_TILE_X DESK_MASTER_X
#define DESK_MASTER_TILE_Y 56
#define DESK_MASTER_TILE_W DESK_MASTER_W
#define DESK_MASTER_TILE_H 372
#define DESK_PANIC_Y 440
#define DESK_PANIC_H 144

#define DESK_GAP 8
#define DESK_RADIUS 12

// Tile sizes: a hook or state 200x64, four to a row; a pick 128x88, six to
// a row; a hold 128x104. Older names kept for the tests that lay by hand.
#define DESK_HOOK_W 200
#define DESK_HOOK_H 64
#define DESK_PICK_W 128
#define DESK_PICK_H 88
#define DESK_HOLD_H 104
#define DESK_COLS 6
#define DESK_GRID_X DESK_CONTENT_X
#define DESK_GRID_Y DESK_CONTENT_Y
#define DESK_TILE_W DESK_HOOK_W
#define DESK_TILE_H DESK_HOOK_H

// The pager for a tab with more than one page: small buttons at the bottom
// right of the content.
#define DESK_PAGER_W 48
#define DESK_PAGER_H 32
#define DESK_PAGER_Y 548

// The palette. Amber means the master says running and nothing else; the
// stop is the one red; a hold-to-fire button has its own violet field.
#define DESK_GLASS      0xFF0E0E10u   // the ground
#define DESK_TILE       0xFF1E1E22u
#define DESK_RAISED     0xFF2A2A30u
#define DESK_LINE       0xFF34343Au
#define DESK_INK        0xFFF5F5F7u
#define DESK_MUTED      0xFF8C8C96u
#define DESK_AMBER      0xFFE08A00u
#define DESK_AMBER_INK  0xFF141414u
#define DESK_WARN       0xFFC03020u
#define DESK_HOLD       0xFF24202Cu
#define DESK_HOLD_LINE  0xFF7860C8u

#endif
