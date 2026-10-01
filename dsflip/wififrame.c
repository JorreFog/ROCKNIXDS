/* 802.11 data/management frames and the RX circular buffer, from the public register notes
 * (akkit DS wifi reference, GBATEK): 12-byte TX/RX headers, FCS is the Ethernet CRC. */
#include "wififrame.h"
#include <string.h>

uint32_t wififrame_crc32(const uint8_t *p, int n) {
    uint32_t c = 0xffffffffu;
    for (int i = 0; i < n; i++) {
        c ^= p[i];
        for (int b = 0; b < 8; b++) c = (c >> 1) ^ (0xedb88320u & -(c & 1));
    }
    return c ^ 0xffffffffu;
}

static void put16(uint8_t *p, unsigned v) { p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; }

static int finish(uint8_t *o, int n, int cap) {
    if (n < 0 || n + 4 > cap) return -1;
    uint32_t c = wififrame_crc32(o, n);
    o[n] = c & 0xff; o[n + 1] = (c >> 8) & 0xff; o[n + 2] = (c >> 16) & 0xff; o[n + 3] = (c >> 24) & 0xff;
    return n + 4;
}

static int mgmt(uint8_t *o, int cap, unsigned fc, const uint8_t *a1, const uint8_t *a2, const uint8_t *a3, unsigned seq) {
    if (cap < 24) return -1;
    put16(o, fc);
    put16(o + 2, 0);
    memcpy(o + 4, a1, 6);
    memcpy(o + 10, a2, 6);
    memcpy(o + 16, a3, 6);
    put16(o + 22, (seq & 0xfff) << 4);
    return 24;
}

static const uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

/* ESS, short preamble, no privacy. Rates are 1/2/5.5/11, the two DS rates marked basic. */
static int put_ssid(uint8_t *p, const char *ssid) {
    if (!ssid || !ssid[0]) ssid = "rocknixds";
    int n = 0; while (ssid[n] && n < 32) n++;
    p[0] = 0; p[1] = n; memcpy(p + 2, ssid, n);
    return 2 + n;
}
static int put_ies(uint8_t *p, const char *ssid, int channel) {
    int n = put_ssid(p, ssid);
    p[n] = 1; p[n + 1] = 4; p[n + 2] = 0x82; p[n + 3] = 0x84; p[n + 4] = 0x8b; p[n + 5] = 0x96; n += 6;
    p[n] = 3; p[n + 1] = 1; p[n + 2] = channel ? channel : 1; n += 3;
    p[n] = 5; p[n + 1] = 4; p[n + 2] = 0; p[n + 3] = 1; p[n + 4] = 0; p[n + 5] = 0; n += 6; /* TIM */
    return n;
}

static int beacon_body(uint8_t *o, int at, const char *ssid, int channel, uint64_t tsf) {
    for (int i = 0; i < 8; i++) o[at++] = (tsf >> (8 * i)) & 0xff;
    put16(o + at, 100); at += 2;          /* beacon interval, TU */
    put16(o + at, 0x0021); at += 2;       /* capability */
    return at + put_ies(o + at, ssid, channel);
}

int wififrame_beacon(uint8_t *o, int cap, const uint8_t bssid[6], const char *ssid, int channel, uint16_t seq, uint64_t tsf) {
    int n = mgmt(o, cap, 0x0080, bcast, bssid, bssid, seq);
    if (n < 0 || n + 64 > cap) return -1;
    n = beacon_body(o, n, ssid, channel, tsf);
    return finish(o, n, cap);
}

int wififrame_probe_resp(uint8_t *o, int cap, const uint8_t bssid[6], const uint8_t da[6], const char *ssid, int channel, uint16_t seq, uint64_t tsf) {
    int n = mgmt(o, cap, 0x0050, da, bssid, bssid, seq);
    if (n < 0 || n + 64 > cap) return -1;
    n = beacon_body(o, n, ssid, channel, tsf);
    return finish(o, n, cap);
}

int wififrame_auth(uint8_t *o, int cap, const uint8_t bssid[6], const uint8_t da[6], uint16_t seq) {
    int n = mgmt(o, cap, 0x00b0, da, bssid, bssid, seq);
    if (n < 0 || n + 6 > cap) return -1;
    put16(o + n, 0); put16(o + n + 2, 2); put16(o + n + 4, 0); /* open, seq 2, success */
    return finish(o, n + 6, cap);
}

int wififrame_assoc_resp(uint8_t *o, int cap, const uint8_t bssid[6], const uint8_t da[6], const char *ssid, uint16_t seq) {
    int n = mgmt(o, cap, 0x0010, da, bssid, bssid, seq);
    if (n < 0 || n + 48 > cap) return -1;
    put16(o + n, 0x0021); put16(o + n + 2, 0); put16(o + n + 4, 0xc001); n += 6; /* cap, status, AID 1 */
    n += put_ies(o + n, ssid, 1);
    return finish(o, n, cap);
}

int wififrame_fromds(uint8_t *o, int cap, const uint8_t da[6], const uint8_t bssid[6], const uint8_t sa[6],
                     uint16_t seq, uint16_t ethertype, const uint8_t *payload, int plen) {
    if (plen < 0 || plen > 1600) return -1;
    int n = mgmt(o, cap, 0x0208, da, bssid, sa, seq); /* from-DS data: addr1 DA, addr2 BSSID, addr3 SA */
    if (n < 0 || n + 8 + plen + 4 > cap) return -1;
    static const uint8_t snap[6] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };
    memcpy(o + n, snap, 6); n += 6;
    o[n] = ethertype >> 8; o[n + 1] = ethertype & 0xff; n += 2;
    if (plen) memcpy(o + n, payload, plen);
    return finish(o, n + plen, cap);
}

int wififrame_ie_ssid(const uint8_t *ies, int n, char *out, int cap) {
    if (cap) out[0] = 0;
    for (int i = 0; i + 2 <= n; ) {
        int id = ies[i], ln = ies[i + 1];
        if (i + 2 + ln > n) return -1;
        if (id == 0) {
            if (ln >= cap) ln = cap > 0 ? cap - 1 : 0;
            if (ln && cap) memcpy(out, ies + i + 2, ln);
            if (cap) out[ln] = 0;
            return ln;
        }
        i += 2 + ln;
    }
    return -1;
}

/* RXHDR[0] low nibble, from the public header notes: 1 beacon, 8 data, 5 ps-poll, else management. Bit 4 is always set. */
static uint16_t rx_flags(const uint8_t *frame, int flen) {
    if (flen < 2) return 0x0010;
    unsigned fc = frame[0] | ((unsigned)frame[1] << 8);
    unsigned type = (fc >> 2) & 3u, subtype = (fc >> 4) & 0xfu, low = 0;
    if (type == 0 && subtype == 8) low = 0x01;
    else if (type == 2 && subtype < 8) low = 0x08;
    else if (type == 1 && subtype == 10) low = 0x05;
    return (uint16_t)(low | 0x0010u);
}

int wififrame_rx_push(uint8_t *mac, uint16_t *wrcsr, uint16_t readcsr, uint16_t begin, uint16_t end,
                      const uint8_t *frame, int flen, int bssid_match) {
    if (!mac || !wrcsr || flen < 0 || flen > 2400) return 0;
    if (begin < 0x4000 || end > 0x6000 || end <= begin || ((begin | end) & 1)) return 0;
    unsigned rec = (12u + (unsigned)flen + 3u) & ~3u;
    unsigned size = (unsigned)(end - begin);
    if (rec >= size) return 0;
    unsigned wr = 0x4000u + (unsigned)(*wrcsr) * 2u;
    unsigned rd = 0x4000u + (unsigned)readcsr * 2u;
    if (wr < begin || wr >= end || rd < begin || rd >= end) return 0;
    unsigned used = wr >= rd ? wr - rd : size - (rd - wr);
    if (rec >= size - used) return 0;                 /* landing on the read cursor looks like an empty fifo */
    uint16_t flags = rx_flags(frame, flen);
    if (bssid_match) flags |= 0x8000;
    uint8_t hdr[12];
    memset(hdr, 0, sizeof hdr);
    hdr[0] = flags & 0xff; hdr[1] = flags >> 8;
    hdr[2] = 0x40;                                    /* normal reception */
    hdr[6] = 0x14;                                    /* 2 Mbit/s */
    hdr[8] = flen & 0xff; hdr[9] = (flen >> 8) & 0xff; /* IEEE length, FCS not included */
    hdr[10] = 0x80; hdr[11] = 0x40;                   /* max / min RSSI */
    for (unsigned i = 0; i < rec; i++) {
        unsigned a = begin + (wr - begin + i) % size;
        uint8_t b = 0;
        if (i < 12) b = hdr[i];
        else if (i < 12u + (unsigned)flen) b = frame[i - 12];
        mac[a & 0x3fff] = b;
    }
    unsigned next = begin + (wr - begin + rec) % size;
    *wrcsr = (uint16_t)((next - 0x4000u) / 2u);
    return 1;
}
