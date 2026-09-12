// A client for wpa_supplicant's control interface: the unix datagram socket
// wpa_cli speaks to, without wpa_cli. One socket asks and gets answers; a
// second, attached, receives the daemon's unsolicited events, so a scan ends
// on `CTRL-EVENT-SCAN-RESULTS` rather than after a guessed four seconds.
// Nothing here spawns a process, and no SSID or key passes through a shell.
#ifndef WPA_CTRL_H
#define WPA_CTRL_H

#include <stddef.h>

struct wpa_ctrl;

// Opens both sockets against the daemon's socket path
// (`/var/run/wpa_supplicant/wlan0`) and attaches for events. NULL with the
// reason on stderr when the daemon is not there. ATTACH waits up to 1 s.
// Local sockets use TMPDIR when set, otherwise /tmp.
struct wpa_ctrl *wpa_ctrl_open(const char *path);

// Sends one command and waits up to `timeout_ms` for its reply. Returns the
// reply's length, NUL-terminated in `buf`, or -1 on a timeout, a socket
// error, or a reply larger than `cap` (which is refused, never cut).
int wpa_ctrl_request(struct wpa_ctrl *c, const char *cmd, char *buf, size_t cap,
                     int timeout_ms);

// Non-blocking request API. begin returns 0 after sending, -1 on an error
// (EBUSY if a request is outstanding). reply returns 1 with a whole,
// NUL-terminated reply, 0 when none is ready, -1 on error or truncation.
// The caller owns the deadline and must abandon on timeout or transport error.
// Re-read request_fd after abandon: both descriptor and local address change.
int wpa_ctrl_begin(struct wpa_ctrl *c, const char *cmd);
int wpa_ctrl_request_fd(const struct wpa_ctrl *c);
int wpa_ctrl_reply(struct wpa_ctrl *c, char *buf, size_t cap);
void wpa_ctrl_abandon(struct wpa_ctrl *c);

// The event socket, for poll(). wpa_ctrl_event reads one event line, its
// "<n>" priority prefix stripped: 1 and the line, 0 when none is waiting,
// -1 when the socket broke.
int wpa_ctrl_event_fd(const struct wpa_ctrl *c);
int wpa_ctrl_event(struct wpa_ctrl *c, char *buf, size_t cap);

// DETACH may wait up to 200 ms; call only outside the running desk loop.
void wpa_ctrl_close(struct wpa_ctrl *c);

#endif
