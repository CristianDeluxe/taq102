#!/bin/sh
# Cross-build the lighting desk for the tablet, without rebuilding the image.
#
# The Buildroot toolchain and sysroot live in the OrbStack machine; the sources
# are read from this Mac over the shared mount. Intermediates and output stay
# under DMXDESK_OUT in this checkout; the VM only reads the shared toolchain.
# The result can later be deployed with tools/run-dmxdesk.sh.
#
# cJSON is compiled straight into the binary rather than added to the image:
# one fewer moving part while the desk is changing every hour. When it settles,
# it becomes a Buildroot package like the others.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
machine=${ORB_MACHINE:-taq102}
out=${DMXDESK_OUT:-$here/output}

[ -f "$here/tools/vendor/cjson/cJSON.c" ] || "$here/tools/get-cjson.sh"

mkdir -p "$out"
orb -m "$machine" -u root bash -lc "
set -eu
SRC=$here/src
VENDOR=$here/tools/vendor/cjson
BUILD=$out/build
export TMPDIR=$out/tmp
SYSROOT=/work/output/host/arm-buildroot-linux-gnueabihf/sysroot
CC=/work/output/host/bin/arm-buildroot-linux-gnueabihf-gcc

mkdir -p \$BUILD/include/cjson \$TMPDIR
cp \$VENDOR/cJSON.h \$BUILD/include/cjson/

# Third party on its own terms; ours under -Werror.
\$CC -O2 -w -I\$SYSROOT/usr/include -c \$VENDOR/cJSON.c -o \$BUILD/cJSON.o

\$CC -std=gnu99 -Wall -Wextra -Werror -O2 \
    -I\$SRC -I\$BUILD/include -I\$SYSROOT/usr/include -I\$SYSROOT/usr/include/libdrm \
    -o \$BUILD/dmxdesk \
    \$SRC/dmxdesk.c \$SRC/desk_model.c \$SRC/desk_master_level_at.c \$SRC/desk_master_track.c \$SRC/desk_rebuild_model.c \$SRC/desk_paint.c \$SRC/desk_present_drm.c \
    \$SRC/desk_layout_resolve.c \$SRC/desk_show_layout.c \$SRC/desk_hold.c \$SRC/desk_build_holds.c \$SRC/desk_caption.c \$SRC/desk_view.c \$SRC/desk_view_pager.c \$SRC/desk_pager_hit.c \$SRC/desk_pager_label.c \$SRC/desk_pager_caption.c \$SRC/desk_pager_bank_caption.c \$SRC/desk_input.c \
    \$SRC/desk_lock.c \$SRC/display_power.c \$SRC/power_key.c \$SRC/perf_window.c \
    \$SRC/showmap.c \$SRC/showmap_validate.c \$SRC/vcjson.c \$SRC/qlc_codec.c \
    \$SRC/qlc_session.c \$SRC/ws_client.c \$SRC/send_queue.c \$SRC/http_fetch.c \
    \$SRC/status.c \$SRC/icon.c \$SRC/desk_fonts.c \$SRC/canvas.c \$SRC/canvas_blend.c \
    \$SRC/font.c \$SRC/touch_input.c \$SRC/touch_flip.c \$SRC/oneeuro.c \
    \$SRC/desk_setup.c \$SRC/desk_setup_paint.c \$SRC/keyboard.c \$SRC/keyboard_paint.c \
    \$SRC/desk_power.c \$SRC/backlight.c \$SRC/settings.c \
    \$SRC/settings_store.c \$SRC/power_policy.c \$SRC/wpa_ctrl_dial.c \$SRC/wpa_ctrl_transact.c \$SRC/wpa_ctrl.c \
    \$SRC/wpa_ctrl_begin.c \$SRC/wpa_ctrl_request_fd.c \$SRC/wpa_ctrl_reply.c \$SRC/wpa_ctrl_abandon.c \
    \$SRC/wpa_ctrl_request.c \$SRC/wpa_ctrl_event_fd.c \$SRC/wpa_ctrl_event.c \$SRC/wpa_ctrl_close.c \$SRC/wifi_scan.c \
    \$SRC/wifi_conf.c \$SRC/wifi_join_network_id.c \$SRC/wifi_join_init.c \$SRC/wifi_join_finish.c \
    \$SRC/wifi_join_advance.c \$SRC/wifi_join_restore.c \$SRC/wifi_join_fail.c \
    \$SRC/wifi_join_start.c \$SRC/wifi_join_start_renewal.c \$SRC/wifi_join_event_matches.c \
    \$SRC/wifi_join.c \$SRC/wifi_join_free.c \$SRC/desk_wifi_request.c \$SRC/wifi_status.c \$SRC/action_worker.c \
    \$SRC/desk_conf.c \$SRC/master_find.c \$SRC/master_find_ports.c \$SRC/master_find_peer_error.c \$SRC/desk_setup_read_found.c \$SRC/iface_prefix.c \
    \$SRC/speed_factor.c \$SRC/desk_tap.c \$SRC/desk_speed.c \$SRC/desk_speed_paint.c \
    \$BUILD/cJSON.o \
    -L\$SYSROOT/usr/lib -ldrm -lm
\$CC -v 2>&1 | tail -1
cp \$BUILD/dmxdesk $out/dmxdesk
"
ls -l "$out/dmxdesk"
file "$out/dmxdesk" 2>/dev/null || true
