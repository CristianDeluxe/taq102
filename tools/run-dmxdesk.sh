#!/bin/sh
# Push the desk to the tablet and run it, without reflashing anything.
#
# The appliance's own application holds the display, so this stops it first and
# leaves the tablet showing the desk. Nothing is written outside /tmp, which is
# a tmpfs, so a reboot brings the appliance back exactly as it was.
#
#   tools/run-dmxdesk.sh --host 192.168.1.50   # master is the show MacBook
#   tools/run-dmxdesk.sh --host "$(ipconfig getifaddr en0)" --port 9998   # this Mac
#   tools/run-dmxdesk.sh --dump frame.ppm      # one frame back here, then exit
#   tools/run-dmxdesk.sh --stop                # put the appliance back
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
tablet=${TAQ102_HOST:-taq102.local}
key=${TAQ102_KEY:-$HOME/.ssh/taq102}
# No master by default: the desk is told one, or (later) keeps its own.
master=""
port=""
map=""
dump=""
view=""
setup=""
stop=0

while [ $# -gt 0 ]; do
	case $1 in
	--host) master=$2; shift 2 ;;
	--port) port=$2; shift 2 ;;
	--map) map=$2; shift 2 ;;
	--dump) dump=$2; shift 2 ;;
	--view) view=$2; shift 2 ;;
	--setup) setup=1; shift ;;
	--stop) stop=1; shift ;;
	*) echo "usage: $0 [--host H] [--port P] [--map FILE] [--dump FILE] [--view P,B] [--setup] [--stop]" >&2; exit 2 ;;
	esac
done

ssh_opts="-i $key -o UserKnownHostsFile=$here/taq102-known-hosts -o StrictHostKeyChecking=accept-new"

# The remote side reads its parameters from the environment and its script from
# a quoted here-document, so this shell never expands the tablet's variables.
remote() {
	remote_map=$1
	remote_dump=$2
	# shellcheck disable=SC2086
	ssh $ssh_opts "root@$tablet" \
		"MASTER='$master' PORT='$port' MAP='$remote_map' DUMP='$remote_dump' VIEW='$view' SETUP='$setup' sh -s" <<'REMOTE'
set -eu
# The appliance holds the display, and taq102-app restarts glcube when it
# dies, so the supervisor goes first. busybox here has no killall and pidof
# does not see a shell script by its own name, hence the ps walk.
for p in $(ps -o pid= -o args= 2>/dev/null | awk '/taq102-app/ && !/awk/ {print $1}'); do
	kill "$p" 2>/dev/null || true
done
for p in $(pidof glcube dmxdesk 2>/dev/null); do kill "$p" || true; done
sleep 1
for p in $(pidof glcube 2>/dev/null); do kill -9 "$p" || true; done
# Mainline registers silead_ts before the power key, so the touch node is
# chosen by device name rather than by its number.
touch_node=/dev/input/event1
power_node=""
for e in /sys/class/input/event*; do
	case "$(cat "$e/device/name" 2>/dev/null)" in
	silead_ts) touch_node=/dev/input/$(basename "$e") ;;
	"rk805 pwrkey") power_node=/dev/input/$(basename "$e") ;;
	esac
done
# --host and --port go only when the caller gave them, so a host the tablet
# keeps for itself (a later phase) is not overridden on every relaunch.
set -- --map "$MAP" --touch "$touch_node"
[ -n "$power_node" ] && set -- "$@" --power "$power_node"
[ -n "$VIEW" ] && set -- "$@" --view "$VIEW"
[ -n "$SETUP" ] && set -- "$@" --setup
[ -n "$MASTER" ] && set -- "$@" --host "$MASTER"
[ -n "$PORT" ] && set -- "$@" --port "$PORT"
if [ -n "$DUMP" ]; then
	DMXDESK_DUMP="$DUMP" /tmp/dmxdesk "$@"
else
	DMXDESK_RTT=1 nohup /tmp/dmxdesk "$@" > /tmp/dmxdesk.log 2>&1 &
	sleep 2
	cat /tmp/dmxdesk.log
fi
REMOTE
}

if [ "$stop" = 1 ]; then
	# shellcheck disable=SC2086
	ssh $ssh_opts "root@$tablet" <<'REMOTE' || true
for p in $(pidof dmxdesk 2>/dev/null); do kill "$p" || true; done
sleep 1
nohup /usr/sbin/taq102-cube app > /dev/null 2>&1 &
REMOTE
	echo "appliance restored on $tablet"
	exit 0
fi

[ -x "$here/output/dmxdesk" ] || "$here/tools/build-dmxdesk.sh"
# The Vibra map ships in spectalive/dmxdesk, at the tag the package pins.
[ -n "$map" ] || map=${DMXDESK_DIR:-$("$here/tools/get-dmxdesk.sh")}/show/vibra.desk.json
map_name=$(basename "$map")
# A running desk holds its own file open, and scp onto a busy binary fails.
# shellcheck disable=SC2086
ssh $ssh_opts "root@$tablet" \
	'for p in $(pidof dmxdesk 2>/dev/null); do kill "$p" || true; done; sleep 1; rm -f /tmp/dmxdesk' || true
# Dropbear has no sftp server, so scp needs its old direct-transfer protocol.
# shellcheck disable=SC2086
scp -O $ssh_opts "$here/output/dmxdesk" "$map" "root@$tablet:/tmp/"

if [ -n "$dump" ]; then
	remote "/tmp/$map_name" /tmp/desk.ppm
	# shellcheck disable=SC2086
	scp -O $ssh_opts "root@$tablet:/tmp/desk.ppm" "$dump"
	echo "wrote $dump"
	exit 0
fi

remote "/tmp/$map_name" ""
echo "desk running on $tablet against $master:$port"
