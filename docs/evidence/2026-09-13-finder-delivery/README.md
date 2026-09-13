# The tablet finds the live master and stays linked

Verified on 2026-09-13 from tablet `192.168.1.71`, over SSH using the repository's known-hosts file.

`tools/build-dmxdesk.sh` completes with GCC 14.3.0 (Buildroot 2026.02.3),
`-Wall -Wextra -Werror`, no compiler warnings, and exit 0. The new source
entries retain the escaped `$SRC` required by the build script.

`TAQ102_HOST=192.168.1.71 tools/run-dmxdesk.sh` deploys the build and leaves
the desk running. SHA-256 values after the final launch:

```text
4b6ce9ecd029d793b8b74f965ee09b89a218455f9570be2b50a612f1a8ad5ed5  output/dmxdesk (Mac)
4b6ce9ecd029d793b8b74f965ee09b89a218455f9570be2b50a612f1a8ad5ed5  /tmp/dmxdesk (tablet)
4b6ce9ecd029d793b8b74f965ee09b89a218455f9570be2b50a612f1a8ad5ed5  /proc/20372/exe (tablet)
```

The acceptance command runs on the tablet, using the gear screen's CLI path:

```sh
/tmp/dmxdesk --find 192.168.1.71/24 9998
```

Its literal stdout, exit 0:

```text
192.168.1.76:9998
```

Elapsed time including SSH and hash collection: 4.184 seconds.
An additional scan with configured port 12345 exits 0 in 5.620 seconds
including SSH and prints:

```text
192.168.1.62:9998
192.168.1.76:9998
```

The second address in that list is the approved master. The `.62` result is
incidental to the subnet sweep and is not selected or configured. An extra
local assertion expecting exactly one result failed on the additional result;
the finder itself succeeded, and both scans discover `.76:9998`.

At entry, `/data/desk.conf` actually contains `.62`, while the running process
overrides it with `--host 192.168.1.65`. Both are stale for this task. The
previous binary and config are preserved in `/tmp/dmxdesk-before-finder` and
`/tmp/desk.conf-before-finder`. The only persistent tablet write changes the
existing desk config, preserving its format:

```text
master=192.168.1.76
port=9998
```

The final deployment passes no host or port override. The process reads that
config, and its log reaches:

```text
desk: link connecting (not connected yet)
desk: link reading the show (not connected yet)
desk: link ready (linked)
```

A subsequent sample still reports `link up (linked)` and RTT values of
9, 10, 16 and 18 ms. PID 20372 remains running.

`tools/test-dmx-desk-host.sh` completes unsandboxed: 39 PASS, 1 FAIL,
`desk_geometry_test`, the deliberately failing separate geometry audit.
There are no socket EPERM failures and no new failing tests. The finder test
measures 768 unique attempts, at most 64 simultaneous sockets, and a
4815 ms silent sweep. The audit sources, reports and runner modification
remain unchanged and outside the finder commit.

Raw local logs: `output/finder-delivery-build.log`,
`output/finder-delivery-tests.log`, `output/finder-delivery-tablet.txt`,
`output/finder-delivery-wrong-port.txt`, `output/finder-delivery-linked.log`,
and `output/finder-delivery-final-state.txt`.
