/* Host test for the wifi frame builder and the userspace NAT. No DraStic, no device.
 *   gcc -O2 -Wall -Wextra -pthread -o /tmp/wfc_test wfcnet.c wififrame.c wfc_test.c && /tmp/wfc_test
 */
#define _GNU_SOURCE
#include "wfcnet.h"
#include "wififrame.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int fails;
static void expect(int cond, const char *msg) {
    if (cond) return;
    fprintf(stderr, "FAIL %s\n", msg);
    fails++;
}

static uint32_t csum_add(uint32_t s, const uint8_t *p, int n) {
    while (n > 1) { s += (p[0] << 8) | p[1]; p += 2; n -= 2; }
    if (n) s += p[0] << 8;
    return s;
}
static int csum_ok(const uint8_t *p, int n) {
    uint32_t s = csum_add(0, p, n);
    while (s >> 16) s = (s & 0xffff) + (s >> 16);
    return s == 0xffff;
}

/* ---------- frames ---------- */
static void test_frames(void) {
    const uint8_t s[] = "123456789";
    expect(wififrame_crc32(s, 9) == 0xcbf43926u, "crc32");
    uint8_t bssid[6] = { 2, 9, 0xbf, 0x11, 0x22, 0x33 };
    uint8_t da[6] = { 2, 0, 0, 0, 0, 1 };
    uint8_t frm[400];
    int n = wififrame_beacon(frm, sizeof frm, bssid, "rocknixds", 1, 7, 0x1234);
    expect(n > 28, "beacon length");
    expect(frm[0] == 0x80 && frm[1] == 0, "beacon fc");
    expect(wififrame_crc32(frm, n - 4) == (uint32_t)(frm[n - 4] | (frm[n - 3] << 8) | (frm[n - 2] << 16) | (frm[n - 1] << 24)), "beacon fcs");
    char ssid[33];
    expect(wififrame_ie_ssid(frm + 36, n - 4 - 36, ssid, sizeof ssid) == 9, "beacon ssid len");
    expect(!strcmp(ssid, "rocknixds"), "beacon ssid");
    n = wififrame_probe_resp(frm, sizeof frm, bssid, da, "saved", 1, 1, 0);
    expect(n > 0 && frm[0] == 0x50, "probe resp");
    expect(wififrame_ie_ssid(frm + 36, n - 4 - 36, ssid, sizeof ssid) == 5 && !strcmp(ssid, "saved"), "probe ssid");
    n = wififrame_auth(frm, sizeof frm, bssid, da, 1);
    expect(n > 24 && frm[0] == 0xb0 && frm[24] == 0 && frm[26] == 2 && frm[28] == 0, "auth open seq 2");
    n = wififrame_assoc_resp(frm, sizeof frm, bssid, da, "rocknixds", 2);
    expect(n > 30 && frm[0] == 0x10 && frm[26] == 0 && (frm[28] | (frm[29] << 8)) == 0xc001, "assoc aid");
    uint8_t payload[4] = { 1, 2, 3, 4 };
    uint8_t sa[6] = { 9, 9, 9, 9, 9, 9 };
    n = wififrame_fromds(frm, sizeof frm, da, bssid, sa, 3, 0x0800, payload, 4);
    expect(n == 24 + 8 + 4 + 4, "from-ds length");
    expect(frm[0] == 0x08 && frm[1] == 0x02, "from-ds fc");
    expect(!memcmp(frm + 4, da, 6) && !memcmp(frm + 10, bssid, 6) && !memcmp(frm + 16, sa, 6), "from-ds addrs");
    expect(frm[24] == 0xaa && frm[30] == 0x08 && frm[31] == 0x00 && !memcmp(frm + 32, payload, 4), "snap");

    uint8_t mac[0x8000];
    memset(mac, 0, sizeof mac);
    /* address 0x4000 is index 0: DraStic stores RAM at addr & 0x3fff. FC 0x0001 is management, so flags are 0x10. */
    uint16_t wr = 0, rd = 0;
    uint8_t one[4] = { 1, 2, 3, 4 };
    expect(wififrame_rx_push(mac, &wr, rd, 0x4000, 0x4800, one, 4, 0) == 1, "rx push base");
    expect(mac[0] == 0x10 && mac[2] == 0x40 && mac[6] == 0x14 && mac[8] == 4 && mac[12] == 1, "rx at index 0");
    /* firmware layout: BEGIN/END are byte addresses, the cursors are halfword indexes (0x4C00 -> 0x0600) */
    memset(mac, 0, sizeof mac);
    wr = rd = 0x0600;
    n = wififrame_beacon(frm, sizeof frm, bssid, "rocknixds", 1, 1, 0);
    expect(n > 4 && wififrame_rx_push(mac, &wr, rd, 0x4c00, 0x5f60, frm, n - 4, 1) == 1, "rx firmware cursor");
    expect(mac[0x0c00] == 0x11 && mac[0x0c01] == 0x80 && (mac[0x0c08] | (mac[0x0c09] << 8)) == n - 4, "beacon header");
    expect(mac[0x0c0c] == 0x80 && wr == 0x0600 + (((12 + (n - 4) + 3) & ~3) / 2), "beacon body, cursor in halfwords");
    /* 64-byte fifo [0x5fc0, 0x6000). A 20-byte record starts 16 bytes before the end, so 4 bytes wrap to 0x5fc0. */
    memset(mac, 0, sizeof mac);
    wr = rd = (0x5ff0 - 0x4000) / 2;
    uint8_t frame[8];
    memset(frame, 0xab, sizeof frame);
    expect(wififrame_rx_push(mac, &wr, rd, 0x5fc0, 0x6000, frame, 8, 0) == 1, "rx push wrap");
    expect(mac[0x5ff0 & 0x3fff] == 0x10 && mac[(0x5ff0 + 8) & 0x3fff] == 8, "rx header");
    expect(mac[(0x5ff0 + 12) & 0x3fff] == 0xab, "rx body");
    expect(mac[0x5fc0 & 0x3fff] == 0xab, "wrapped tail");
    expect(wr == (0x5fc4 - 0x4000) / 2, "cursor wrapped");
    int pushed = 1;
    for (int i = 0; i < 8; i++) pushed += wififrame_rx_push(mac, &wr, rd, 0x5fc0, 0x6000, frame, 8, 0);
    expect(pushed < 9 && mac[0x5ff0 & 0x3fff] == 0x10, "rx drop when full, first header kept");
}

/* ---------- nat ---------- */
struct cap {
    uint8_t pkt[8][2048];
    int len[8], n;
    uint8_t dst[8][6];
    uint16_t et[8];
};
static void on_inject(void *ud, const uint8_t dst[6], const uint8_t src[6], uint16_t et,
                      const uint8_t *payload, int len) {
    (void)src;
    struct cap *c = ud;
    if (c->n >= 8 || len < 0 || len > 2048) return;
    memcpy(c->pkt[c->n], payload, len);
    memcpy(c->dst[c->n], dst, 6);
    c->len[c->n] = len; c->et[c->n] = et; c->n++;
}
static void logln(void *ud, const char *line) { (void)ud; fprintf(stderr, "  %s\n", line); }

static uint8_t ds_mac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x10 };
static uint8_t gw_mac[6] = { 0x02, 0x09, 0xbf, 0x11, 0x22, 0x33 };
static uint32_t ds_ip, gw_ip;

static void put16(uint8_t *p, unsigned v) { p[0] = v >> 8; p[1] = v; }
static void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static unsigned rd16(const uint8_t *p) { return (p[0] << 8) | p[1]; }
static uint32_t rd32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static int ip_pkt(uint8_t *o, uint32_t src, uint32_t dst, int proto, const uint8_t *l4, int l4len) {
    int tot = 20 + l4len;
    memset(o, 0, 20);
    o[0] = 0x45; o[2] = tot >> 8; o[3] = tot; o[8] = 64; o[9] = proto;
    memcpy(o + 12, &src, 4); memcpy(o + 16, &dst, 4);
    uint32_t s = csum_add(0, o, 20);
    while (s >> 16) s = (s & 0xffff) + (s >> 16);
    unsigned c = ~s & 0xffff;
    o[10] = c >> 8; o[11] = c;
    memcpy(o + 20, l4, l4len);
    return tot;
}
static wfcnet *make_net(struct cap *c, uint32_t dns) {
    memset(c, 0, sizeof *c);
    wfcnet_cfg cfg; memset(&cfg, 0, sizeof cfg);
    cfg.ds_ip = ds_ip; cfg.gw_ip = gw_ip; cfg.dns_ip = dns;
    memcpy(cfg.gw_mac, gw_mac, 6);
    cfg.inject = on_inject; cfg.log = logln; cfg.ud = c; cfg.debug = 1;
    wfcnet *n = wfcnet_create(&cfg);
    wfcnet_note_mac(n, ds_mac);
    return n;
}
static void feed_ip(wfcnet *n, const uint8_t *ip, int len) {
    wfcnet_input(n, gw_mac, ds_mac, 0x0800, ip, len);
}

static void test_dhcp_arp_icmp(void) {
    struct cap c; uint32_t dns = htonl(0xb23e2bd4u);
    wfcnet *n = make_net(&c, dns);
    uint8_t bootp[300]; memset(bootp, 0, sizeof bootp);
    bootp[0] = 1; bootp[1] = 1; bootp[2] = 6; put32(bootp + 4, 0x11223344);
    memcpy(bootp + 28, ds_mac, 6);
    bootp[236] = 0x63; bootp[237] = 0x82; bootp[238] = 0x53; bootp[239] = 0x63;
    bootp[240] = 53; bootp[241] = 1; bootp[242] = 1; bootp[243] = 255;
    uint8_t udp[8 + 300];
    put16(udp, 68); put16(udp + 2, 67); put16(udp + 4, 8 + 244);
    memcpy(udp + 8, bootp, 244);
    uint8_t ip[600];
    int iplen = ip_pkt(ip, 0, htonl(0xffffffffu), 17, udp, 8 + 244);
    feed_ip(n, ip, iplen);
    expect(c.n == 1 && c.et[0] == 0x0800, "dhcp inject");
    expect(csum_ok(c.pkt[0], 20), "dhcp ip csum");
    const uint8_t *o = c.pkt[0];
    expect(o[9] == 17 && !memcmp(o + 16, "\xff\xff\xff\xff", 4), "dhcp broadcast");
    const uint8_t *b = o + 20 + 8;
    expect(b[0] == 2 && !memcmp(b + 16, &ds_ip, 4), "dhcp yiaddr");
    int found_dns = 0, mtype = 0;
    for (int i = 240; i < 400 && b[i] != 255; ) {
        if (b[i] == 0) { i++; continue; }
        int l = b[i + 1];
        if (b[i] == 53 && l == 1) mtype = b[i + 2];
        if (b[i] == 6 && l == 4 && !memcmp(b + i + 2, &dns, 4)) found_dns = 1;
        i += 2 + l;
    }
    expect(mtype == 2 && found_dns, "dhcp offer dns");

    c.n = 0;
    uint8_t arp[28]; memset(arp, 0, sizeof arp);
    put16(arp, 1); put16(arp + 2, 0x0800); arp[4] = 6; arp[5] = 4; put16(arp + 6, 1);
    memcpy(arp + 8, ds_mac, 6); memcpy(arp + 14, &ds_ip, 4); memcpy(arp + 24, &gw_ip, 4);
    wfcnet_input(n, gw_mac, ds_mac, 0x0806, arp, 28);
    expect(c.n == 1 && c.et[0] == 0x0806 && rd16(c.pkt[0] + 6) == 2, "arp reply");
    expect(!memcmp(c.pkt[0] + 8, gw_mac, 6) && !memcmp(c.pkt[0] + 14, &gw_ip, 4), "arp gw");

    c.n = 0;
    uint8_t echo[16]; memset(echo, 0, sizeof echo);
    echo[0] = 8; echo[4] = 0x12; echo[6] = 0x34; memcpy(echo + 8, "ping", 4);
    uint32_t s = csum_add(0, echo, 16); while (s >> 16) s = (s & 0xffff) + (s >> 16);
    unsigned cs = ~s & 0xffff; echo[2] = cs >> 8; echo[3] = cs;
    iplen = ip_pkt(ip, ds_ip, gw_ip, 1, echo, 16);
    feed_ip(n, ip, iplen);
    expect(c.n == 1 && c.pkt[0][20] == 0 && !memcmp(c.pkt[0] + 28, "ping", 4), "icmp echo");
    expect(csum_ok(c.pkt[0] + 20, 16), "icmp csum");
    wfcnet_destroy(n);
}

struct srv { int fd; int got; char data[64]; };
static void *udp_srv(void *a) {
    struct srv *s = a;
    struct sockaddr_in from; socklen_t sl = sizeof from;
    int n = recvfrom(s->fd, s->data, sizeof s->data, 0, (struct sockaddr *)&from, &sl);
    if (n > 0) { s->got = n; sendto(s->fd, s->data, n, 0, (struct sockaddr *)&from, sl); }
    return 0;
}
static void *tcp_srv(void *a) {
    struct srv *s = a;
    int c = accept(s->fd, 0, 0);
    if (c < 0) return 0;
    int fl = fcntl(c, F_GETFL, 0); fcntl(c, F_SETFL, fl | O_NONBLOCK);
    int off = 0;
    for (int i = 0; i < 200 && off < 4; i++) {
        int n = recv(c, s->data + off, sizeof s->data - off, 0);
        if (n > 0) off += n;
        else usleep(5000);
    }
    s->got = off;
    if (off) send(c, s->data, off, 0);
    usleep(30000);
    close(c);
    return 0;
}
static int listen_udp(struct srv *s) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(0x7f000001);
    if (bind(fd, (struct sockaddr *)&a, sizeof a)) { close(fd); return -1; }
    socklen_t sl = sizeof a; getsockname(fd, (struct sockaddr *)&a, &sl);
    s->fd = fd; s->got = 0;
    return ntohs(a.sin_port);
}
static int listen_tcp(struct srv *s) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(0x7f000001);
    if (bind(fd, (struct sockaddr *)&a, sizeof a) || listen(fd, 1)) { close(fd); return -1; }
    socklen_t sl = sizeof a; getsockname(fd, (struct sockaddr *)&a, &sl);
    s->fd = fd; s->got = 0;
    return ntohs(a.sin_port);
}

static void tcp_segment(uint8_t *o, int *len, uint16_t sport, uint16_t dport, uint32_t seq, uint32_t ack,
                        unsigned flags, const uint8_t *data, int dlen) {
    uint8_t seg[64]; memset(seg, 0, 20);
    put16(seg, sport); put16(seg + 2, dport); put32(seg + 4, seq); put32(seg + 8, ack);
    seg[12] = 5 << 4; seg[13] = flags; put16(seg + 14, 1400);
    if (dlen) memcpy(seg + 20, data, dlen);
    int total = 20 + dlen;
    uint8_t ph[12]; memcpy(ph, &ds_ip, 4); uint32_t dst = htonl(0x7f000001); memcpy(ph + 4, &dst, 4);
    ph[8] = 0; ph[9] = 6; ph[10] = total >> 8; ph[11] = total;
    uint32_t s = csum_add(csum_add(0, ph, 12), seg, total);
    while (s >> 16) s = (s & 0xffff) + (s >> 16);
    unsigned c = ~s & 0xffff; seg[16] = c >> 8; seg[17] = c;
    *len = ip_pkt(o, ds_ip, dst, 6, seg, total);
}

static int l4_ok(const uint8_t *ip, int len) {
    if (len < 20 || !csum_ok(ip, 20)) return 0;
    int ihl = (ip[0] & 0xf) * 4, proto = ip[9], l4 = len - ihl;
    uint8_t ph[12];
    memcpy(ph, ip + 12, 8); ph[8] = 0; ph[9] = proto; ph[10] = l4 >> 8; ph[11] = l4;
    uint32_t s = csum_add(csum_add(0, ph, 12), ip + ihl, l4);
    while (s >> 16) s = (s & 0xffff) + (s >> 16);
    return s == 0xffff;
}

static void test_udp(void) {
    struct srv sv; int port = listen_udp(&sv);
    expect(port > 0, "udp bind");
    pthread_t th; pthread_create(&th, 0, udp_srv, &sv);
    struct cap c; wfcnet *n = make_net(&c, htonl(0x7f000001));
    uint8_t uh[8 + 4]; memset(uh, 0, 8);
    put16(uh, 5000); put16(uh + 2, port); put16(uh + 4, 12); memcpy(uh + 8, "dns?", 4);
    uint8_t ip[64]; int len = ip_pkt(ip, ds_ip, htonl(0x7f000001), 17, uh, 12);
    feed_ip(n, ip, len);
    int saw = 0;
    for (int i = 0; i < 50 && !saw; i++) {
        c.n = 0; wfcnet_poll(n); usleep(10000);
        for (int k = 0; k < c.n; k++) {
            if (c.et[k] != 0x0800 || c.len[k] < 28) continue;
            if (c.pkt[k][9] != 17) continue;
            if (rd16(c.pkt[k] + 22) == 5000 && c.len[k] >= 32 && !memcmp(c.pkt[k] + 28, "dns?", 4)) saw = l4_ok(c.pkt[k], c.len[k]);
        }
    }
    expect(saw, "udp echo");
    expect(sv.got == 4, "udp server got it");
    pthread_join(th, 0); close(sv.fd); wfcnet_destroy(n);
}

static void test_tcp(void) {
    struct srv sv; int port = listen_tcp(&sv);
    expect(port > 0, "tcp bind");
    pthread_t th; pthread_create(&th, 0, tcp_srv, &sv);
    struct cap c; wfcnet *n = make_net(&c, htonl(0x7f000001));
    uint8_t ip[128]; int len;
    tcp_segment(ip, &len, 4000, port, 1000, 0, 0x02, 0, 0); /* SYN */
    c.n = 0; feed_ip(n, ip, len);
    expect(c.n >= 1, "syn-ack sent");
    const uint8_t *sa = 0; int salen = 0;
    for (int k = 0; k < c.n; k++) if (c.pkt[k][9] == 6) { sa = c.pkt[k]; salen = c.len[k]; }
    expect(sa && (sa[20 + 13] & 0x12) == 0x12, "syn-ack flags");
    expect(sa && l4_ok(sa, salen), "syn-ack csum");
    uint32_t iss = sa ? rd32(sa + 24) : 0;
    expect(sa && rd32(sa + 28) == 1001, "syn-ack ack");
    /* leave it unacked long enough to retransmit once */
    int syns = 1;
    for (int i = 0; i < 20 && syns < 2; i++) {
        usleep(50000); c.n = 0; wfcnet_poll(n);
        for (int k = 0; k < c.n; k++) if (c.pkt[k][9] == 6 && (c.pkt[k][33] & 0x02)) syns++;
    }
    expect(syns >= 2, "syn-ack retransmit");
    tcp_segment(ip, &len, 4000, port, 1001, iss + 1, 0x10, 0, 0);
    c.n = 0; feed_ip(n, ip, len);
    uint8_t hello[4] = { 'p', 'i', 'n', 'g' };
    tcp_segment(ip, &len, 4000, port, 1001, iss + 1, 0x18, hello, 4);
    feed_ip(n, ip, len);
    int got = 0; uint32_t seq = 0; int dlen = 0;
    for (int i = 0; i < 80 && !got; i++) {
        c.n = 0; wfcnet_poll(n); usleep(10000);
        for (int k = 0; k < c.n; k++) {
            if (c.pkt[k][9] != 6 || c.len[k] < 40) continue;
            int hlen = (c.pkt[k][32] >> 4) * 4;
            int pl = c.len[k] - 20 - hlen;
            if (pl >= 4 && !memcmp(c.pkt[k] + 20 + hlen, "ping", 4)) {
                got = l4_ok(c.pkt[k], c.len[k]);
                seq = rd32(c.pkt[k] + 24); dlen = pl;
            }
        }
    }
    expect(got, "tcp echo");
    if (got) {
        tcp_segment(ip, &len, 4000, port, 1005, seq + dlen, 0x10, 0, 0);
        feed_ip(n, ip, len);
    }
    pthread_join(th, 0);
    expect(sv.got == 4 && !memcmp(sv.data, "ping", 4), "tcp server got ping");
    close(sv.fd); wfcnet_destroy(n);
}

int main(void) {
    ds_ip = htonl(0x0a0d2514u);
    gw_ip = htonl(0x0a0d2501u);
    test_frames();
    test_dhcp_arp_icmp();
    test_udp();
    test_tcp();
    if (fails) { fprintf(stderr, "%d failed\n", fails); return 1; }
    printf("wfc_test ok\n");
    return 0;
}
