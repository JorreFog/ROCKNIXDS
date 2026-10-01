/* 802.11 frames for the DraStic wifi hook. No DraStic types: the host test links this. */
#ifndef WIFIFRAME_H
#define WIFIFRAME_H
#include <stdint.h>

uint32_t wififrame_crc32(const uint8_t *p, int n);

/* Each builder writes one 802.11 frame, FCS included, and returns its length (or -1). */
int wififrame_beacon(uint8_t *o, int cap, const uint8_t bssid[6], const char *ssid, int channel, uint16_t seq, uint64_t tsf);
int wififrame_probe_resp(uint8_t *o, int cap, const uint8_t bssid[6], const uint8_t da[6], const char *ssid, int channel, uint16_t seq, uint64_t tsf);
int wififrame_auth(uint8_t *o, int cap, const uint8_t bssid[6], const uint8_t da[6], uint16_t seq);
int wififrame_assoc_resp(uint8_t *o, int cap, const uint8_t bssid[6], const uint8_t da[6], const char *ssid, uint16_t seq);
int wififrame_fromds(uint8_t *o, int cap, const uint8_t da[6], const uint8_t bssid[6], const uint8_t sa[6],
                     uint16_t seq, uint16_t ethertype, const uint8_t *payload, int plen);

/* SSID information element. Returns the length, 0 for a wildcard, -1 if the element is absent. */
int wififrame_ie_ssid(const uint8_t *ies, int n, char *out, int cap);

/* mac[] is the wifi address window: index with addr & 0x3fff (a 32 KiB buffer).
 * begin/end are byte addresses (0x4C00..0x5F60). wrcsr/readcsr are halfword indexes: byte = 0x4000 + reg*2.
 * frame is the IEEE header and body, without the FCS. bssid_match sets RX-header bit 15. Returns 1 if queued. */
int wififrame_rx_push(uint8_t *mac, uint16_t *wrcsr, uint16_t readcsr, uint16_t begin, uint16_t end,
                      const uint8_t *frame, int flen, int bssid_match);
#endif
