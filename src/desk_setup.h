// The settings surface: what the operator can change from the tablet alone.
// A pure model: two cards (the Wi-Fi and the master), the brightness fader
// and the power-aware toggle, a keyboard when a key or an address is being
// typed, and a confirmation before a join. Every touch produces at most one
// action for the caller, who owns the sockets, the files and the runtime.
#ifndef DESK_SETUP_H
#define DESK_SETUP_H

#include "keyboard.h"
#include "wifi_scan.h"

#define SETUP_HOST_MAX 128
#define SETUP_FOUND_MAX 16
#define SETUP_WORD_MAX 32

enum setup_action_kind {
    SETUP_NONE,
    SETUP_SCAN,             // ask the supplicant to scan
    SETUP_JOIN,             // the operator confirmed: join ssid with psk (empty for open)
    SETUP_FIND,             // sweep the subnet for masters
    SETUP_SET_MASTER,       // host and port chosen or typed
    SETUP_BRIGHTNESS,       // value 8..max, the fader moved
    SETUP_POWER_AWARE,      // value 0/1, the toggle
    SETUP_CLOSE,            // the surface asked to close (a tap outside a card)
};

struct setup_action {
    enum setup_action_kind kind;
    char ssid[WIFI_SSID_MAX];
    char psk[64];
    int known;              // SETUP_JOIN: the key is on file, psk is empty
    char host[SETUP_HOST_MAX];
    int port;
    int value;
};

struct desk_setup {
    int open;
    // The Wi-Fi card.
    char ssid[WIFI_SSID_MAX];           // the network the supplicant is on, or empty
    char wifi_state[SETUP_WORD_MAX];    // the supplicant's word: COMPLETED, SCANNING, ...
    char address[64];
    struct wifi_scan scan;
    int known[WIFI_SCAN_MAX];           // has a block in the config
    int scan_page;
    char wifi_busy[SETUP_WORD_MAX];     // "Scanning", "Joining", a stage word; empty when idle
    int wifi_available;                 // the control socket answered
    char wifi_note[48];                 // the last outcome: "Wrong key", "Joined TestNet5"
    // The master card.
    char master[SETUP_HOST_MAX];
    int port;
    int master_configured;
    char link_word[SETUP_WORD_MAX];
    char found[SETUP_FOUND_MAX][SETUP_HOST_MAX];
    int found_count;
    int found_partial;
    int found_page;
    char master_busy[SETUP_WORD_MAX];
    char master_note[48];               // "Search failed", "No network address", "Applied, not saved" 
    // The footer.
    int brightness, brightness_max, power_aware;
    int brightness_unsaved;             // the level applied but the file refused it
    // Dialogs.
    struct keyboard kb;
    enum { KB_FOR_NOTHING, KB_FOR_PSK, KB_FOR_HOST } kb_purpose;
    char pending_ssid[WIFI_SSID_MAX];
    int confirm_open;                   // the join confirmation sheet
    char confirm_psk[64];
    int confirm_is_open_network;
    int confirm_known;
    // Touch.
    int capture;                        // enum setup_target, private
    int capture_index;
    int dragging_fader;
    int dirty;
};

void desk_setup_init(struct desk_setup *s);
void desk_setup_open(struct desk_setup *s);
void desk_setup_close(struct desk_setup *s);

// Touches in panel pixels; at most one action per release (a fader drag
// produces one per move).
struct setup_action desk_setup_touch_down(struct desk_setup *s, int x, int y);
struct setup_action desk_setup_touch_move(struct desk_setup *s, int x, int y);
struct setup_action desk_setup_touch_up(struct desk_setup *s, int x, int y);
void desk_setup_touch_cancel(struct desk_setup *s);

// Facts the caller feeds in.
void desk_setup_set_scan(struct desk_setup *s, const struct wifi_scan *scan, const int *known);
void desk_setup_set_wifi(struct desk_setup *s, const char *ssid, const char *state, const char *address);
void desk_setup_set_found(struct desk_setup *s, const char (*hosts)[SETUP_HOST_MAX], int count, int partial);
void desk_setup_set_master(struct desk_setup *s, const char *host, int port, int configured);

#endif
