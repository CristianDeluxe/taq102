// The chrome a finger can reach outside the content: the rail's page entries,
// the bank selector, the lock target. Rectangles shared by painting and hit
// testing, so a thing is pressed exactly where it is drawn.
#ifndef DESK_VIEW_H
#define DESK_VIEW_H

struct desk_rect { int x, y, w, h; };

#define DESK_RAIL_ENTRY_H 44
#define DESK_RAIL_FIRST_Y 64
#define DESK_LOCK_SIZE 72
#define DESK_SELECTOR_H 56
#define DESK_BANK_PILL_W 72

// The rail entry for page `index`, 140 wide, 44 tall, from y 64 down.
struct desk_rect desk_view_rail_entry(int index);
// The lock target: a 72 px square on the rail above the link word.
struct desk_rect desk_view_lock_target(void);
// The bank pill for bank `index`, in the 56 px strip at the bottom of the
// content area, 72 wide with 16 px gaps from x 156.
struct desk_rect desk_view_bank_button(int index);

int desk_rect_contains(struct desk_rect r, int x, int y);

#endif
