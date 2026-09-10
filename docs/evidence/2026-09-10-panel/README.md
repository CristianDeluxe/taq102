# Evidence, 2026-09-10: the panel measured after the "tembleques" came back

Instrument: the iPhone as a Continuity Camera over USB-C on the Mac mini, the
project's own `tools/panel-camera/zigzag.py` for horizontal displacement and the
new `flicker.py` for brightness. The camera did not move between measurements,
which is the only thing that makes the variant ranking meaningful.

## The PHY variant sweep, panel px rms of frame-to-frame movement

Full-screen single-pixel vertical lines, `lvdsdiag <variant> vlines`, crop
1400x100 in the middle of the panel, 60 frames each.

| variant | moving |
| --- | --- |
| **kernel baseline** (no lvdsdiag, straight after a reboot) | **0.008** |
| pll336 | 0.006 |
| vendor | 0.009 |
| msbsel-off | 0.098 |
| stock-order | 0.203 |
| source-e4 | 0.215 |
| forward | 0.221 |
| ours | 0.411 |

The floor is 0.006, which matches the 0.005 measured on 2026-09-05 with a
different camera. The kernel's own configuration sits on that floor, and the
live registers read `prediv 2, fbdiv 28` -- the vendor pair. **The LVDS link is
clean and the PLL is not what I is seeing.** `lvdsdiag`'s variant named
`ours` is the pre-fix 350 MHz configuration and is 70x worse, which is what
makes the floor credible: the instrument can still see the old fault.

## Brightness, same rig

Static `testpattern vlines`, one buffer, no flips, no GPU:

| crop | whole-frame variation | banding within a frame |
| --- | --- | --- |
| panel, 30 fps | 0.11 % | 2.2 levels rms |
| black bezel, 30 fps | 2.66 % | 5.7 levels rms |
| panel, 60 fps | 0.37 % | 3.1 levels rms |
| black bezel, 60 fps | 1.73 % | 5.4 levels rms |

The bezel emits nothing and bands harder than the panel, so the banding is the
room's mains light, not the display. The panel itself is flat.

## The cube, 30 s at 60 fps

1800 frames, brightness sd 8.7 (the cube turning), no frame below 60 % of the
median, largest frame-to-frame change 1.63 levels, no outlier jumps. The
`flip_done` blackout in `TODO.md` did not occur in that window.

## What this leaves

Nothing the camera can see with a static image, at 30 or 60 fps, on either
axis. The remaining candidate is the page-flip path, which `testpattern`
bypasses entirely and which the cube exercises with moving content, where a
displacement measurement cannot separate artifact from motion. `src/fliptest.c`
is the instrument for that -- a static scene page-flipped between two
bit-identical buffers -- and it is not in the mainline image's `taq102-diag`
package yet.
