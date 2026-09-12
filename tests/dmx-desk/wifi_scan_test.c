// SOURCES: wifi_scan.c
// The real scan table from the tablet on 2026-09-12, plus the cases the
// venue will throw at it: duplicates, a hidden network, an enterprise one.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "wifi_scan.h"

int main(void) {
    const char table[] =
        "bssid / frequency / signal level / flags / ssid\n"
        "00:00:5e:00:53:01\t2437\t-34\t[WPA2-PSK-CCMP][ESS]\tTestNet\n"
        "00:00:5e:00:53:04\t2437\t-34\t[WPA2-PSK-CCMP][ESS]\tArloBase-0000000000\n"
        "00:00:5e:00:53:03\t2457\t-36\t[WPA2-PSK-CCMP][ESS]\tTestNet5\n"
        "00:00:5e:00:53:05\t2437\t-38\t[WPA2-PSK-CCMP][ESS]\tSUN2000-HV0000000000\n"
        "00:00:5e:00:53:06\t2437\t-78\t[WPA2-PSK-CCMP][ESS]\tMaximumLengthNetworkName32Chars_\n"
        "00:00:5e:00:53:07\t2437\t-110\t[WPA2-PSK-CCMP][ESS][P2P]\tDIRECT-00-Printer Model 0000\n"
        "aa:bb:cc:dd:ee:01\t5180\t-60\t[WPA2-PSK-CCMP][ESS]\tTestNet\n"
        "aa:bb:cc:dd:ee:02\t5180\t-50\t[ESS]\t\n"
        "aa:bb:cc:dd:ee:03\t5180\t-52\t[WPA2-EAP-CCMP][ESS]\teduroam\n"
        "aa:bb:cc:dd:ee:04\t5180\t-55\t[WPA3-SAE-CCMP][ESS]\tOnlySae\n"
        "aa:bb:cc:dd:ee:05\t5180\t-56\t[WPA2-PSK-CCMP][SAE][ESS]\tMixed\n"
        "aa:bb:cc:dd:ee:06\t5180\t-57\t[ESS]\tCafe Abierto\n"
        "broken line without tabs\n";
    struct wifi_scan scan;
    int n = wifi_scan_parse(table, sizeof table - 1, &scan);
    assert(n == 10);
    // Strongest first; TestNet once, at its strongest.
    assert(strcmp(scan.network[0].ssid, "TestNet") == 0 || strcmp(scan.network[0].ssid, "ArloBase-0000000000") == 0);
    int seen = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(scan.network[i].ssid, "TestNet") == 0) {
            seen++;
            assert(scan.network[i].level_dbm == -34);
        }
        if (i > 0)
            assert(scan.network[i].level_dbm <= scan.network[i - 1].level_dbm);
        assert(scan.network[i].ssid[0]);
    }
    assert(seen == 1);
    const struct wifi_network *w = NULL;
    for (int i = 0; i < n; i++) if (strcmp(scan.network[i].ssid, "eduroam") == 0) w = &scan.network[i];
    assert(w && w->security == WIFI_EAP && !wifi_scan_joinable(w->security));
    for (int i = 0; i < n; i++) if (strcmp(scan.network[i].ssid, "OnlySae") == 0) w = &scan.network[i];
    assert(w && w->security == WIFI_SAE_ONLY && !wifi_scan_joinable(w->security));
    for (int i = 0; i < n; i++) if (strcmp(scan.network[i].ssid, "Mixed") == 0) w = &scan.network[i];
    assert(w && w->security == WIFI_PSK && wifi_scan_joinable(w->security));
    for (int i = 0; i < n; i++) if (strcmp(scan.network[i].ssid, "Cafe Abierto") == 0) w = &scan.network[i];
    assert(w && w->security == WIFI_OPEN && wifi_scan_joinable(w->security));
    for (int i = 0; i < n; i++) if (strcmp(scan.network[i].ssid, "DIRECT-00-Printer Model 0000") == 0) w = &scan.network[i];
    assert(w && w->level_dbm == -110);
    assert(wifi_scan_parse("", 0, &scan) == 0);
    assert(wifi_scan_parse("just a header\n", 14, &scan) == 0);
    printf("wifi_scan ok\n");
    return 0;
}
