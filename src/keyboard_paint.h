// Draws the keyboard sheet: title, field, keys. Reads the model only.
#ifndef KEYBOARD_PAINT_H
#define KEYBOARD_PAINT_H

#include "canvas.h"
#include "desk_paint.h"
#include "keyboard.h"

void keyboard_paint(struct canvas *c, const struct keyboard *kb, const struct desk_fonts *fonts);

#endif
