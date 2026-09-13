# Keep the 750 ms link deadline and give heartbeat replies time to arrive

The diagnostic build disproved the proposed 800 ms desk-loop stall for the
three reproduced drops. The desk discarded WebSocket Ping/Pong activity when
updating its silence clock. A packet capture caught one false drop after the
desk had answered the master's Ping only 605 ms earlier. The master also took
513 ms to answer an API heartbeat in that event; the old RTT log could not
establish that every request received a prompt response.

## Measurement before changing behavior

The original running executable was
`4b6ce9ecd029d793b8b74f965ee09b89a218455f9570be2b50a612f1a8ad5ed5`.
It and its log were preserved as `/tmp/dmxdesk-before-stall` and
`/tmp/dmxdesk-before-stall.log` on the tablet.

Diagnostic executable SHA-256:
`0f0d80bcb0db5067b8bff6c1ed139dc10ef0a8e8a1c7e012bcbe37b9dfd4d9cf`.
PID 4286 ran against `192.168.1.76:9998`. Monotonic timings covered each
iteration, poll, power and touch input, supplicant work, finder work, QLC
session step, received frames and model work, status reads, power policy,
painting, presentation, reporting, and snapshots. Inside `status_read`, the
address ioctl, `/proc/net/wireless`, each power-supply file read, and the total
power read had independent timers. Calls/iterations exceeding 200 ms logged
with their phase and monotonic end time. ATTACH/DETACH were timed at startup
and shutdown; neither occurs repeatedly on the normal loop.

The first ten-minute window starts at the first READY log, monotonic
183517862 ms, and ends at 184117862 ms. It contains exactly three drops:

| Tablet monotonic time (ms) | Reported text silence (ms) | Preceding iteration over 200 ms |
| --- | ---: | --- |
| 183630317 | 806 | None |
| 183641146 | 804 | None |
| 183917976 | 835 | None |

No status/Wi-Fi/power-read phase exceeded 200 ms. The 205-212 ms QLC step
measurements occurred during snapshot parsing after reconnecting, not before
the drops. Initial connection/reconnection iterations were 272-292 ms.

The third drop is captured on the Mac's wired interface. These timestamps
are the Mac packet clock; tablet monotonic timestamps are a different clock.
The TCP connection is tablet port 45040 to master port 9998.

| Mac UTC, 2026-09-13 | Packet/event |
| --- | --- |
| 19:48:02.094513 | Master sends the previous API response |
| 19:48:02.320969 | Master sends a WebSocket Ping |
| 19:48:02.334723 | Tablet sends the matching Pong |
| 19:48:02.435954 | Mac receives the next API heartbeat |
| 19:48:02.461554 | Mac TCP acknowledges that request |
| 19:48:02.838285 | Tablet sends another API heartbeat |
| 19:48:02.939545 | Tablet initiates TCP close after its false stale decision |
| 19:48:02.949237 | Master sends both API responses in one packet |

The first API response waited 513.283 ms on the Mac side. There are no TCP
retransmissions or sequence gaps in this captured event. The exact source of
the Mac-side delay is not established: this capture does not distinguish
QLC+ dispatch/processing from host scheduling. The other two drops occurred
before packet capture began, so their precise packet-level cause is unknown.

The previous RTT clock was overwritten at every 400 ms probe. In addition,
responses arriving after a disconnect never become RTT samples. Thus the
reported low RTT distribution was incomplete evidence for excluding delays.

## A second measured failure: transport recovery needs time

The first fix (control-frame activity plus an honest RTT clock) still dropped
once in its ten-minute window: monotonic 184643228, with 807 ms since the last
activity but only 405 ms since sending its outstanding probe. No desk-loop
stall preceded this drop either.

The second capture shows the Mac receiving that heartbeat at 20:00:08.004058
UTC and replying at 20:00:08.010991, in 6.933 ms. That response was not
acknowledged by the tablet, and the Mac retransmitted it at 20:00:08.131386.
At 20:00:08.187307 the tablet initiated close while still acknowledging the
sequence number *before* the response. Thus this event includes a transport
delivery failure, not a slow QLC+ response or a blocked desk loop. The capture
is on the Mac; it cannot identify which radio/AP/driver component lost or
delayed the packets. Power save remained on, but its role is unproven and it
was not changed. See `transport-event.txt`.

Waiting 400 ms to probe consumes more than half the 750 ms silence deadline
before transport has a chance to deliver a reply. The observed 513 ms reply
already exceeds that remaining budget without any loop stall.

## Fix

`ws_recv_text` now reports activity separately from text delivery. Complete
Ping/Pong control frames refresh `qlc_session`'s last-heard clock even when no
text is returned. Empty reads and incomplete messages do not refresh it;
invalid fragmented/oversized control frames are rejected. Ping responses are
still sent through the existing nonblocking queue.

Only one API probe is outstanding at a time, so its send timestamp survives
until its response and delayed replies have an honest RTT. A stale decision
also logs the last activity and outstanding-probe age.

`STALE_MS = 750` remains unchanged. `DESK_HEARTBEAT_MS` is now 100 ms:
with a 100 ms idle-loop poll, that leaves about 550 ms for a reply inside the
same silence deadline. This is a deliberate probe-scheduling correction from
the measured 513 ms response and the separate retransmission event; it does
not add a grace period after 750 ms. A single outstanding probe bounds work
while the peer is slow. No PMIC read was moved to a worker because none of the measured drops implicated it. QLC+
was not stopped, restarted, reconfigured, or sent show-changing commands.

The final build retains phase instrumentation; slow iterations now also
print all phase durations, including phases individually below 200 ms.

## Validation and deployment

The host regression replays the captured pattern with a Ping at +230 ms,
probe at +400 ms, stale check at +805 ms, and API reply at +914 ms (514 ms
RTT). The old session logic fails the +805 ms READY assertion. The corrected
logic remains READY, records 514 ms, and still drops at exactly 751 ms of
subsequent real silence; commands are refused after that drop. Lower-level
tests cover Ping/Pong activity, activity reset, partial headers, incomplete
text, and invalid fragmented Ping frames. A second session replay allows a
100 ms poll, then a 514 ms response with no intervening Ping. It fails with
the old 400 ms cadence and passes with the actual shared 100 ms desk constant;
real silence still drops at 751 ms.

`tools/test-dmx-desk-host.sh`: 41 PASS, 0 FAIL, exit 0, with ASan/UBSan.
ARM build: GCC 14.3.0, `-Wall -Wextra -Werror`, exit 0.

All deployments were built from an archived HEAD under
`output/link-stall/source`, with only this task's changes. HEAD `a7b4894`
differs from the running finder commit `29ba341` only in TODO documentation.
Uncommitted geometry changes were preserved locally and excluded from the
deployed executables. No commit was made.

First-fix executable SHA-256:
`68ac620e528b298308c15522000c1c752026b1eb2bc3d0d7503b161030b66bd1`.
The first-fix process started as PID 6137, with explicit master
`192.168.1.76:9998`. The diagnostic executable/log remain on the tablet as
`/tmp/dmxdesk-stall-diagnostic` and `/tmp/dmxdesk-stall-diagnostic.log`.

The first-fix ten-minute window was [184237434, 184837434) monotonic ms:
**1 drop**, at 184643228. Its last completed RTT maximum was 276 ms, again
excluding the unanswered probe at the drop. The subsequent 100 ms cadence
build and completed final ten-minute window are recorded below.

## Final build

Final executable SHA-256:
`46bc80ccbe325083b766fa868b623932619d5353259dea556a34e1f15833d848`.
The deployed artifact is `output/link-stall/final/dmxdesk`. PID 8050 started
with explicit `--host 192.168.1.76 --port 9998` and reached READY at tablet
monotonic 184972682 ms. The final comparison window ends at 185572682 ms.

The final gate again passed all 41 tests, with ASan/UBSan and exit 0.
The old-cadence replay fails at the no-reply-yet check after 514 ms of waiting;
the final replay passes. The ARM build again completed without warnings.

`output/link-stall/regression-only.patch` contains only the regression's
source/tests, including instrumentation. `git apply --check` passed against
copies of the HEAD files. It excludes the pre-existing geometry edits and
TODO changes; those remain separate in the worktree. A direct comparison of
the deployed `dmxdesk.c` and the working copy shows only the preserved
geometry input-guard hunk. Unrelated original tracked diffs were checked
unchanged. No Git commit or index mutation was performed.

The ten-minute windows are consecutive live observations, not a controlled
replay of my touches. Owner interaction was not controlled. The
host tests replay the timed failures deterministically, and the final packet
capture tests live recovery under continuing retransmissions.

## Evidence files

Local raw evidence is under `output/link-stall/`: `before-window.json`,
`diagnostic-live.log`, `qlc.pcap`, `qlc-after.pcap`, `analyze_capture.py`,
`fixed-host-gate.log`, `fixed-build.log`, `fixed-deploy.log`,
`pre-fix-replay.log`, `old-timing-replay.log`, and `qlc-session-test-v2.log`. The original instrumenting
script is retained as `diagnostic-instrument.py`.

Protocol reference: [RFC 6455, Ping and Pong](https://www.rfc-editor.org/rfc/rfc6455.html#section-5.5.2).

## Completed observation after resuming the interrupted session

The final ten-minute window [184972682, 185572682) tablet monotonic ms
completed with **zero drops**, versus **three drops** in the diagnostic
window of equal duration. The intermediate control-frame-only fix had one.
At observation time 185603370 ms, 630688 ms had elapsed since READY; the
entire retrieved log also contained zero drops. Its 4009 RTT samples had a
maximum of 381 ms. RTT counts cover the full retrieved log, not exactly the
ten-minute comparison window, because individual RTT lines have no timestamp.

The only slow final-build iteration was the initial show load: 278 ms,
including 191 ms in `qlc-step`, 66 ms painting, and 13 ms presenting. There
were no subsequent iterations over 200 ms in the retrieved log and no slow
PMIC/status phase. The 477.903-second final packet capture contains three
retransmitted data segments and no close on WebSocket port 60476. Its 3378
matched API responses have a maximum Mac-side response time of 127.820 ms.
This capture ends before the ten-minute log window; it establishes recovery
under some continuing retransmissions, not ten minutes of packet coverage.
See `final-packet-summary.txt` and `final-window.json`.

At **2026-09-13 20:15:52 UTC**, direct tablet verification found PID 8050,
running executable hash `46bc80ccbe325083b766fa868b623932619d5353259dea556a34e1f15833d848`,
explicit `--host 192.168.1.76 --port 9998`, an ESTABLISHED socket from
192.168.1.71:60476 to 192.168.1.76:9998, and the live report
`link up (linked)`. No additional deployment or build was needed on resumption.

The saved final host gate contains 41 PASS and no FAIL, including the geometry
test run by the previous attempt. On resumption, the geometry audit and full
gate were **not rerun**. The three existing sanitizer test binaries
`qlc_session_test`, `ws_activity_test`, and `ws_client_test` were run serially
and all passed on resumption. `git diff --check` also passed. Source comparison confirmed that the deployed
regression files match the working files; the `dmxdesk.c` difference is solely
the preserved geometry input guard, excluded from deployment. No code changes,
commit, index changes, or QLC+ restart were made on resumption.

The exact Mac-side delay source and the failed packet delivery component
remain unexplained. Ten minutes without a drop demonstrates improvement in
this observation; it does not prove an entire show will be drop-free. The
phase instrumentation remains enabled to capture any future loop stall.
