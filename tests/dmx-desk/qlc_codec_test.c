// SOURCES: qlc_codec.c
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "qlc_codec.h"

static struct qlc_msg decoded(const char *frame) {
    struct qlc_msg m;
    memset(&m, 0xAA, sizeof m);
    assert(qlc_decode(frame, strlen(frame), &m) == 0);
    return m;
}

static void refused(const char *frame) {
    struct qlc_msg m;
    assert(qlc_decode(frame, strlen(frame), &m) == -1);
}

static void encodes(int len, const char *buf, const char *expected) {
    assert(len == (int)strlen(expected));
    assert(strcmp(buf, expected) == 0);
}

int main(void) {
    char b[128];

    // The frames this desk saw on the wire on 2026-09-12, pressing widget 98.
    struct qlc_msg m = decoded("98|BUTTON|255");
    assert(m.kind == QLC_BUTTON && m.widget_id == 98 && m.value == 255);
    m = decoded("FUNCTION|6|Running");
    assert(m.kind == QLC_FUNCTION && m.function_id == 6 && m.running == 1);
    m = decoded("FUNCTION|6|Stopped");
    assert(m.kind == QLC_FUNCTION && m.function_id == 6 && m.running == 0);
    m = decoded("QLC+API|isProjectLoaded|false");
    assert(m.kind == QLC_API && strcmp(m.query, "isProjectLoaded") == 0 &&
           strcmp(m.payload, "false") == 0);
    m = decoded("GM_VALUE|128|50%");
    assert(m.kind == QLC_GRAND_MASTER && m.value == 128);
    m = decoded("42|SLIDER|64");
    assert(m.kind == QLC_SLIDER && m.widget_id == 42 && m.value == 64);

    // Function id 0 is a real function in this show (EFX "Movimiento Circulo").
    m = decoded("FUNCTION|0|Running");
    assert(m.kind == QLC_FUNCTION && m.function_id == 0 && m.running == 1);

    // A known shape carrying an unreadable number is refused, never guessed.
    refused("98|BUTTON|");
    refused("98|BUTTON|abc");
    refused("98|BUTTON|25 5");
    refused("98|BUTTON|-1");
    refused("98|BUTTON|256");
    refused("-3|BUTTON|255");
    refused("99999999999999999999|BUTTON|255");
    refused("FUNCTION|6|Sideways");
    refused("FUNCTION||Running");
    refused("GM_VALUE|999|x");
    refused("");
    refused("|");
    refused("BUTTON");

    // A frame the desk does not act on reads as unknown rather than failing,
    // so one unhandled push cannot drop the link.
    m = decoded("54|SPEED_STATE|500|0|0");
    assert(m.kind == QLC_UNKNOWN);
    m = decoded("0|XYPAD|12.50|200.00");
    assert(m.kind == QLC_UNKNOWN);

    // Frames arrive without a NUL and may carry anything at all after them.
    const char raw[] = "98|BUTTON|255|trailing garbage";
    struct qlc_msg cut;
    assert(qlc_decode(raw, 13, &cut) == 0);
    assert(cut.kind == QLC_BUTTON && cut.widget_id == 98 && cut.value == 255);
    assert(qlc_decode("98|BUTTON|255", 12, &cut) == 0);    // the length is the frame
    assert(cut.value == 25);
    const char embedded[] = "FUNCTION\0|6|Running";
    assert(qlc_decode(embedded, sizeof embedded - 1, &cut) == -1);

    // A hostile payload is truncated into its field and stays NUL-terminated.
    char long_api[256];
    memcpy(long_api, "QLC+API|", 8);
    memset(long_api + 8, 'q', 200);
    long_api[208] = '\0';
    m = decoded(long_api);
    assert(m.kind == QLC_API && strlen(m.query) == sizeof m.query - 1);
    assert(m.payload[0] == '\0');
    char long_payload[512];
    int n = snprintf(long_payload, sizeof long_payload, "QLC+API|getFunctionsList|");
    memset(long_payload + n, 'x', 400);
    long_payload[n + 400] = '\0';
    m = decoded(long_payload);
    assert(strcmp(m.query, "getFunctionsList") == 0);
    assert(strlen(m.payload) == sizeof m.payload - 1);

    // Encoders.
    encodes(qlc_encode_toggle(b, sizeof b, 98), b, "98|255");
    encodes(qlc_encode_flash(b, sizeof b, 125, 1), b, "125|255");
    encodes(qlc_encode_flash(b, sizeof b, 125, 0), b, "125|0");
    encodes(qlc_encode_level(b, sizeof b, 42, 64), b, "42|64");
    encodes(qlc_encode_grand_master(b, sizeof b, 255), b, "GM_VALUE|255");
    encodes(qlc_encode_speed_ms(b, sizeof b, 54, 500), b, "54|SPEED_TIME|500");
    encodes(qlc_encode_slider_release(b, sizeof b, 42), b, "42|SLIDER_OVERRIDE|0");
    encodes(qlc_encode_color(b, sizeof b, 42, 0xFF0000, 0), b,
            "42|CNG_COLORS|#ff0000|#000000");

    // The pad's units are DMX coarse plus a fine fraction, two decimals, and
    // the widget's own range clamps them.
    encodes(qlc_encode_xy(b, sizeof b, 0, 0.0f, 0.0f, 0, 255, 0, 255), b,
            "0|XYPAD|0.00|0.00");
    encodes(qlc_encode_xy(b, sizeof b, 0, 1.0f, 1.0f, 0, 255, 0, 255), b,
            "0|XYPAD|255.00|255.00");
    encodes(qlc_encode_xy(b, sizeof b, 0, 0.5f, 0.25f, 0, 255, 0, 255), b,
            "0|XYPAD|127.50|63.75");
    encodes(qlc_encode_xy(b, sizeof b, 0, 0.9f, 0.9f, 0, 10, 20, 30), b,
            "0|XYPAD|10.00|30.00");
    encodes(qlc_encode_xy(b, sizeof b, 0, -5.0f, 9.0f, 0, 255, 0, 255), b,
            "0|XYPAD|0.00|255.00");

    // Refusals: bad arguments and buffers that do not fit, without writing
    // past the end.
    assert(qlc_encode_toggle(b, sizeof b, -1) == -1);
    assert(qlc_encode_level(b, sizeof b, 42, 256) == -1);
    assert(qlc_encode_level(b, sizeof b, 42, -1) == -1);
    assert(qlc_encode_grand_master(b, sizeof b, 256) == -1);
    assert(qlc_encode_speed_ms(b, sizeof b, 54, -1) == -1);
    assert(qlc_encode_color(b, sizeof b, 42, 0x1000000, 0) == -1);
    assert(qlc_encode_xy(b, sizeof b, 0, 0.5f, 0.5f, 200, 100, 0, 255) == -1);
    assert(qlc_encode_xy(b, sizeof b, 0, nanf(""), 0.5f, 0, 255, 0, 255) == -1);

    char small[7];
    memset(small, 'Z', sizeof small);
    assert(qlc_encode_toggle(small, 6, 98) == -1);           // "98|255" needs 7
    assert(small[6] == 'Z');
    assert(qlc_encode_toggle(small, 7, 98) == 6);
    assert(qlc_encode_grand_master(small, sizeof small, 255) == -1);

    return 0;
}
