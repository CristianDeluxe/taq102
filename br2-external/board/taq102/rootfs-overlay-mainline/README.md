# The mainline-only overlay

One file, and it is here rather than in the shared overlay so that a rebuild of
the vendor-kernel image stays byte-identical.

`lib/firmware/silead/gsl3673.fw` is the touch controller's firmware, 4719
records and 37752 bytes, repacked from the tablet's own stock configuration
array by `kernel/mainline/taq102-gsl3673-firmware.py`. Mainline's `silead.c`
requests it asynchronously: without it there is no error, just no touchscreen.
