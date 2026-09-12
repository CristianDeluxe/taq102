// Where the keys are. The sheet covers the rail and the content, never the
// master column: 0..844 wide, from under the status bar to the bottom. Ten
// keys of 74 with 8 px gaps span 812 px from x 16, so every key is a real
// finger target; the numeric layout has 96 px keys in a keypad.
#ifndef KEYBOARD_LAYOUT_H
#define KEYBOARD_LAYOUT_H

#define KB_SHEET_X 0
#define KB_SHEET_Y 48
#define KB_SHEET_W 844
#define KB_SHEET_H 552

#define KB_TITLE_Y 64
#define KB_FIELD_X 16
#define KB_FIELD_Y 104
#define KB_FIELD_W 812
#define KB_FIELD_H 48

#define KB_KEY_W 74
#define KB_KEY_H 64
#define KB_GAP 8
#define KB_LEFT 16
#define KB_ROW_Y(row) (168 + (row) * 72)    // rows 0..4: 168, 240, 312, 384, 456
#define KB_KEY_X(col) (KB_LEFT + (col) * (KB_KEY_W + KB_GAP))
#define KB_RADIUS 12

// The numeric keypad: 96 px keys, three columns plus backspace, four rows,
// then Cancel and Done.
#define KB_NUM_W 96
#define KB_NUM_H 64
#define KB_NUM_LEFT 200
#define KB_NUM_ROW_Y(row) (168 + (row) * 72)
#define KB_NUM_X(col) (KB_NUM_LEFT + (col) * (KB_NUM_W + KB_GAP))

#endif
