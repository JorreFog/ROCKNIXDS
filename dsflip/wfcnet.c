/* DHCP, ARP, ICMP echo, UDP NAT and a client-only TCP NAT.
 * One outstanding TCP segment per connection: the DS retransmits, and so do we.
 * Replies to the DS are injected as Ethernet frames; the caller wraps them in 802.11. */
#define _GNU_SOURCE
#include "wfcnet.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define UDP_N 32
#define TCP_N 16
#define MSS 1360
#define HOLD 8192
#define RETX_MS 250
#define RETX_MAX 20

struct udp_flow {
    int fd;
    uint32_t rip;
    uint16_t rport, dport;
    uint64_t last;
};
struct tcp_flow {
    int fd, st;                         /* 1 connecting, 2 open, 3 our FIN sent */
    uint32_t rip;
    uint16_t rport, dport;
    uint32_t snd_una, snd_nxt, rcv_nxt, iss;
    uint8_t hold[HOLD];
    int hold_len;
    uint8_t seg[MSS];
    int seg_len, seg_syn, seg_fin, seg_live, seg_tries, fin_pend, fin_sent, ds_fin;
    uint32_t seg_seq;
    uint64_t seg_ms, last;
};
struct wfcnet {
    wfcnet_cfg cfg;
    uint8_t ds_mac[6];
    int have_mac;
    uint16_t ipid;
    struct udp_flow udp[UDP_N];
    struct tcp_flow tcp[TCP_N];
    int dhcp_logged;
};

static uint64_t now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000ull + t.tv_nsec / 1000000ull;
}
static void nlog(wfcnet *n, const char *fmt, ...) {
    if (!n->cfg.log) return;
    char b[240];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    n->cfg.log(n->cfg.ud, b);
}
static void dbg(wfcnet *n, const char *fmt, ...) {
    if (!n->cfg.debug || !n->cfg.log) return;
    char b[240];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    n->cfg.log(n->cfg.ud, b);
}
static uint32_t rnd32(void) {
    static uint32_t s = 0x6d2b79f5u;
    s = s * 1664525u + 1013904223u + (uint32_t)now_ms();
    return s ? s : 1;
}
static int seq_ge(uint32_t a, uint32_t b) { return (int32_t)(a - b) >= 0; }

static uint32_t csum_add(uint32_t s, const uint8_t *p, int n) {
    while (n > 1) { s += (p[0] << 8) | p[1]; p += 2; n -= 2; }
    if (n) s += p[0] << 8;
    return s;
}
static uint16_t csum_fold(uint32_t s) {
    while (s >> 16) s = (s & 0xffff) + (s >> 16);
    return (uint16_t)~s;
}
static uint16_t ip_csum(const uint8_t *h, int n) { return csum_fold(csum_add(0, h, n)); }
static uint16_t l4_csum(uint32_t src, uint32_t dst, int proto, const uint8_t *l4, int len) {
    uint8_t ph[12];
    memcpy(ph, &src, 4); memcpy(ph + 4, &dst, 4);
    ph[8] = 0; ph[9] = proto; ph[10] = len >> 8; ph[11] = len & 0xff;
    uint16_t c = csum_fold(csum_add(csum_add(0, ph, 12), l4, len));
    if (proto == 17 && c == 0) c = 0xffff;        /* UDP: 0 means "no checksum" */
    return c;
}

static void inject(wfcnet *n, const uint8_t dst[6], uint16_t etype, const uint8_t *payload, int len) {
    if (n->cfg.inject) n->cfg.inject(n->cfg.ud, dst, n->cfg.gw_mac, etype, payload, len);
}
static const uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

static void emit_ip(wfcnet *n, uint32_t src, uint32_t dst, int proto, const uint8_t *l4, int l4len, int broadcast) {
    if (l4len < 0 || l4len > 1600) return;
    uint8_t pkt[20 + 1600];
    int tot = 20 + l4len;
    uint16_t id = ++n->ipid;
    memset(pkt, 0, 20);
    pkt[0] = 0x45; pkt[2] = tot >> 8; pkt[3] = tot & 0xff;
    pkt[4] = id >> 8; pkt[5] = id & 0xff;
    pkt[6] = 0x40; pkt[8] = 64; pkt[9] = proto;
    memcpy(pkt + 12, &src, 4);
    uint32_t dip = broadcast ? htonl(0xffffffffu) : dst;
    memcpy(pkt + 16, &dip, 4);
    uint16_t c = ip_csum(pkt, 20);
    pkt[10] = c >> 8; pkt[11] = c & 0xff;
    memcpy(pkt + 20, l4, l4len);
    const uint8_t *da = (broadcast || !n->have_mac) ? bcast : n->ds_mac;
    inject(n, da, 0x0800, pkt, tot);
}

static void put16(uint8_t *p, unsigned v) { p[0] = v >> 8; p[1] = v & 0xff; }
static void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static unsigned rd16(const uint8_t *p) { return (p[0] << 8) | p[1]; }
static uint32_t rd32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static void set_nb(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

/* ---------- TCP ---------- */
static void tcp_free(struct tcp_flow *t) {
    if (t->fd >= 0) close(t->fd);
    memset(t, 0, sizeof *t);
    t->fd = -1;
}
static void tcp_rst(wfcnet *n, struct tcp_flow *t) {
    uint8_t seg[20];
    memset(seg, 0, sizeof seg);
    put16(seg, t->dport); put16(seg + 2, t->rport);
    put32(seg + 4, t->snd_nxt); put32(seg + 8, t->rcv_nxt);
    seg[12] = 5 << 4; seg[13] = 0x14;             /* RST|ACK */
    uint16_t c = l4_csum(t->rip, n->cfg.ds_ip, 6, seg, 20);
    seg[16] = c >> 8; seg[17] = c & 0xff;
    emit_ip(n, t->rip, n->cfg.ds_ip, 6, seg, 20, 0);
    dbg(n, "tcp rst :%u -> %u.%u.%u.%u:%u", t->dport,
        ((uint8_t *)&t->rip)[0], ((uint8_t *)&t->rip)[1], ((uint8_t *)&t->rip)[2], ((uint8_t *)&t->rip)[3], t->rport);
    tcp_free(t);
}
static void tcp_send(wfcnet *n, struct tcp_flow *t, unsigned flags, const uint8_t *data, int len, uint32_t seq, int track) {
    uint8_t seg[24 + MSS];
    int hlen = (flags & 0x02) ? 24 : 20;
    if (len < 0 || len > MSS || hlen + len > (int)sizeof seg) return;
    memset(seg, 0, hlen);
    put16(seg, t->dport); put16(seg + 2, t->rport);
    put32(seg + 4, seq); put32(seg + 8, t->rcv_nxt);
    seg[12] = (hlen / 4) << 4; seg[13] = flags;
    unsigned win = (t->hold_len + MSS < HOLD) ? MSS : 0;
    put16(seg + 14, win);
    if (flags & 0x02) { seg[20] = 2; seg[21] = 4; put16(seg + 22, MSS); }
    if (len) memcpy(seg + hlen, data, len);
    int total = hlen + len;
    uint16_t c = l4_csum(t->rip, n->cfg.ds_ip, 6, seg, total);
    seg[16] = c >> 8; seg[17] = c & 0xff;
    emit_ip(n, t->rip, n->cfg.ds_ip, 6, seg, total, 0);
    if (!track) return;
    t->seg_live = 1; t->seg_seq = seq; t->seg_len = len;
    t->seg_syn = (flags & 0x02) ? 1 : 0; t->seg_fin = (flags & 0x01) ? 1 : 0;
    if (len) memcpy(t->seg, data, len);
    t->seg_ms = now_ms(); t->seg_tries = 0;
}
static void tcp_rexmit(wfcnet *n, struct tcp_flow *t) {
    unsigned flags = 0x10;
    if (t->seg_syn) flags |= 0x02;
    if (t->seg_fin) flags |= 0x01;
    if (t->seg_len) flags |= 0x08;
    int tries = t->seg_tries + 1;
    tcp_send(n, t, flags, t->seg_len ? t->seg : 0, t->seg_len, t->seg_seq, 1);
    t->seg_tries = tries;
}
static struct tcp_flow *tcp_find(wfcnet *n, uint16_t dport, uint32_t rip, uint16_t rport) {
    for (int i = 0; i < TCP_N; i++)
        if (n->tcp[i].fd >= 0 && n->tcp[i].dport == dport && n->tcp[i].rip == rip && n->tcp[i].rport == rport)
            return &n->tcp[i];
    return 0;
}
static struct tcp_flow *tcp_slot(wfcnet *n) {
    for (int i = 0; i < TCP_N; i++) if (n->tcp[i].fd < 0) return &n->tcp[i];
    return 0;
}
static void tcp_flush(wfcnet *n, struct tcp_flow *t) {
    while (t->st >= 2 && t->hold_len && t->fd >= 0) {
        int w = send(t->fd, t->hold, t->hold_len, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            tcp_rst(n, t); return;
        }
        if (!w) return;
        memmove(t->hold, t->hold + w, t->hold_len - w);
        t->hold_len -= w;
    }
}
static int tcp_connect(struct tcp_flow *t) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    set_nb(fd);
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons(t->rport); a.sin_addr.s_addr = t->rip;
    int rc = connect(fd, (struct sockaddr *)&a, sizeof a);
    if (rc < 0 && errno != EINPROGRESS) { close(fd); return -1; }
    t->fd = fd;
    t->st = rc == 0 ? 2 : 1;
    return 0;
}
static void tcp_input(wfcnet *n, uint32_t rip, const uint8_t *tcp, int len) {
    if (len < 20) return;
    int hlen = (tcp[12] >> 4) * 4;
    if (hlen < 20 || hlen > len) return;
    uint16_t dport = rd16(tcp), rport = rd16(tcp + 2);
    uint32_t seq = rd32(tcp + 4), ack = rd32(tcp + 8);
    unsigned flags = tcp[13];
    const uint8_t *data = tcp + hlen;
    int dlen = len - hlen;
    struct tcp_flow *t = tcp_find(n, dport, rip, rport);
    if (!t) {
        if ((flags & 0x02) && !(flags & 0x10)) {      /* SYN */
            t = tcp_slot(n);
            if (!t) return;
            memset(t, 0, sizeof *t); t->fd = -1;
            t->rip = rip; t->rport = rport; t->dport = dport;
            t->iss = rnd32(); t->snd_una = t->iss; t->snd_nxt = t->iss + 1;
            t->rcv_nxt = seq + 1; t->last = now_ms();
            if (tcp_connect(t)) { t->fd = -1; return; }
            tcp_send(n, t, 0x12, 0, 0, t->iss, 1);    /* SYN|ACK immediately; the host connect finishes in poll */
            uint8_t *ipb = (uint8_t *)&rip;
            nlog(n, "tcp %u.%u.%u.%u:%u from :%u", ipb[0], ipb[1], ipb[2], ipb[3], rport, dport);
        }
        return;
    }
    t->last = now_ms();
    if (flags & 0x04) { tcp_free(t); return; }        /* RST */
    if (flags & 0x10) {
        if (seq_ge(ack, t->snd_una) && seq_ge(t->snd_nxt, ack)) {
            if (t->seg_live && seq_ge(ack, t->seg_seq + (uint32_t)t->seg_len + t->seg_syn + t->seg_fin))
                t->seg_live = 0;
            t->snd_una = ack;
        }
    }
    if ((flags & 0x02) && !(flags & 0x10)) {          /* retransmitted SYN */
        if (!t->seg_live || t->seg_syn) tcp_rexmit(n, t);
        return;
    }
    if (dlen && seq != t->rcv_nxt) {
        if (!seq_ge(seq, t->rcv_nxt)) {               /* duplicate: ACK what we have */
            int skip = (int)(t->rcv_nxt - seq);
            if (skip >= dlen) dlen = 0;
            else { data += skip; dlen -= skip; seq += skip; }
        } else dlen = 0;                              /* a hole: wait for the retransmission */
    }
    int took = 0;
    if (dlen && seq == t->rcv_nxt) {
        if (t->hold_len + dlen <= HOLD) {
            memcpy(t->hold + t->hold_len, data, dlen);
            t->hold_len += dlen; t->rcv_nxt += dlen; took = 1;
        }
    }
    if ((flags & 0x01) && !t->ds_fin && (dlen == 0 || took) &&
        seq + (took ? (uint32_t)dlen : 0) == t->rcv_nxt) {
        t->rcv_nxt += 1; t->ds_fin = 1; took = 1;
        if (t->fd >= 0) shutdown(t->fd, SHUT_WR);
    }
    if (took) tcp_send(n, t, 0x10, 0, 0, t->snd_nxt, 0);
    tcp_flush(n, t);
}
static void tcp_poll(wfcnet *n, struct tcp_flow *t) {
    if (t->fd < 0) return;
    if (t->st == 1) {
        struct pollfd p = { .fd = t->fd, .events = POLLOUT };
        if (poll(&p, 1, 0) > 0) {
            int err = 0; socklen_t sl = sizeof err;
            getsockopt(t->fd, SOL_SOCKET, SO_ERROR, &err, &sl);
            if (err) { tcp_rst(n, t); return; }
            t->st = 2;
        }
    }
    tcp_flush(n, t);
    if (t->fd < 0) return;
    if (t->st >= 2 && !t->seg_live && !t->fin_sent) {
        uint8_t buf[MSS];
        int r = recv(t->fd, buf, sizeof buf, 0);
        if (r > 0) {
            tcp_send(n, t, 0x18, buf, r, t->snd_nxt, 1); /* PSH|ACK */
            t->snd_nxt += r;
        } else if (r == 0) t->fin_pend = 1;
        else if (errno != EAGAIN && errno != EWOULDBLOCK) { tcp_rst(n, t); return; }
    }
    if (t->fin_pend && !t->seg_live && !t->fin_sent && t->fd >= 0) {
        tcp_send(n, t, 0x11, 0, 0, t->snd_nxt, 1);    /* FIN|ACK */
        t->snd_nxt += 1; t->fin_sent = 1; t->st = 3;
    }
    if (t->seg_live && now_ms() - t->seg_ms >= RETX_MS) {
        if (t->seg_tries >= RETX_MAX) { tcp_rst(n, t); return; }
        tcp_rexmit(n, t);
    }
    if (t->fin_sent && !t->seg_live && t->ds_fin) tcp_free(t);
}

/* ---------- UDP ---------- */
static void udp_close(struct udp_flow *u) { if (u->fd >= 0) close(u->fd); memset(u, 0, sizeof *u); u->fd = -1; }
static struct udp_flow *udp_find(wfcnet *n, uint16_t dport, uint32_t rip, uint16_t rport, int alloc) {
    struct udp_flow *free_slot = 0;
    for (int i = 0; i < UDP_N; i++) {
        if (n->udp[i].fd < 0) { if (!free_slot) free_slot = &n->udp[i]; continue; }
        if (n->udp[i].dport == dport && n->udp[i].rip == rip && n->udp[i].rport == rport) return &n->udp[i];
    }
    if (!alloc || !free_slot) return 0;
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return 0;
    set_nb(fd);
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons(rport); a.sin_addr.s_addr = rip;
    if (connect(fd, (struct sockaddr *)&a, sizeof a)) { close(fd); return 0; }
    memset(free_slot, 0, sizeof *free_slot);
    free_slot->fd = fd; free_slot->rip = rip; free_slot->rport = rport; free_slot->dport = dport;
    return free_slot;
}
static void udp_input(wfcnet *n, uint32_t rip, const uint8_t *udp, int len) {
    if (len < 8) return;
    uint16_t dport = rd16(udp), rport = rd16(udp + 2);
    int ulen = rd16(udp + 4);
    if (ulen < 8 || ulen > len) ulen = len;
    struct udp_flow *u = udp_find(n, dport, rip, rport, 1);
    if (!u) return;
    u->last = now_ms();
    if (ulen > 8) send(u->fd, udp + 8, ulen - 8, MSG_NOSIGNAL);
    if (n->cfg.debug && rport == 53)
        dbg(n, "dns query %u bytes", ulen - 8);
}
static void udp_poll(wfcnet *n, struct udp_flow *u) {
    if (u->fd < 0) return;
    if (u->last && now_ms() - u->last > 60000) { udp_close(u); return; }
    uint8_t buf[1600];
    int r = recv(u->fd, buf, sizeof buf, 0);
    if (r < 0) return;
    uint8_t uh[8 + 1600];
    int ulen = 8 + r;
    put16(uh, u->rport); put16(uh + 2, u->dport); put16(uh + 4, ulen); uh[6] = uh[7] = 0;
    memcpy(uh + 8, buf, r);
    uint16_t c = l4_csum(u->rip, n->cfg.ds_ip, 17, uh, ulen);
    uh[6] = c >> 8; uh[7] = c & 0xff;
    emit_ip(n, u->rip, n->cfg.ds_ip, 17, uh, ulen, 0);
    if (n->cfg.debug && u->rport == 53) dbg(n, "dns reply %d bytes", r);
}

/* ---------- DHCP / ARP / ICMP ---------- */
static int dhcp_msg(const uint8_t *opt, int n) {
    for (int i = 0; i < n; ) {
        int t = opt[i];
        if (t == 255) break;
        if (t == 0) { i++; continue; }
        if (i + 1 >= n) break;
        int l = opt[i + 1];
        if (i + 2 + l > n) break;
        if (t == 53 && l >= 1) return opt[i + 2];
        i += 2 + l;
    }
    return -1;
}
static void dhcp_reply(wfcnet *n, const uint8_t *bootp, int mtype) {
    uint8_t pkt[548];
    memset(pkt, 0, sizeof pkt);
    pkt[0] = 2; pkt[1] = 1; pkt[2] = 6;             /* reply, ethernet, hlen */
    memcpy(pkt + 4, bootp + 4, 4);                  /* xid */
    memcpy(pkt + 8, bootp + 8, 2);
    put16(pkt + 10, 0x8000);                        /* broadcast flag: the DS has no address yet */
    memcpy(pkt + 16, &n->cfg.ds_ip, 4);             /* yiaddr */
    memcpy(pkt + 20, &n->cfg.gw_ip, 4);             /* siaddr */
    memcpy(pkt + 28, bootp + 28, 16);               /* chaddr */
    pkt[236] = 0x63; pkt[237] = 0x82; pkt[238] = 0x53; pkt[239] = 0x63;
    uint8_t *o = pkt + 240;
    o[0] = 53; o[1] = 1; o[2] = mtype; o += 3;
    o[0] = 54; o[1] = 4; memcpy(o + 2, &n->cfg.gw_ip, 4); o += 6;
    o[0] = 51; o[1] = 4; put32(o + 2, 86400); o += 6;
    o[0] = 1; o[1] = 4; o[2] = 255; o[3] = 255; o[4] = 255; o[5] = 0; o += 6;
    o[0] = 3; o[1] = 4; memcpy(o + 2, &n->cfg.gw_ip, 4); o += 6;
    o[0] = 6; o[1] = 4; memcpy(o + 2, &n->cfg.dns_ip, 4); o += 6; /* the melonDS-style DNS */
    o[0] = 255;
    int blen = (int)(o + 1 - pkt);
    uint8_t udp[8 + 548];
    put16(udp, 67); put16(udp + 2, 68); put16(udp + 4, 8 + blen); udp[6] = udp[7] = 0;
    memcpy(udp + 8, pkt, blen);
    uint32_t bcast_ip = htonl(0xffffffffu);
    uint16_t c = l4_csum(n->cfg.gw_ip, bcast_ip, 17, udp, 8 + blen);
    udp[6] = c >> 8; udp[7] = c & 0xff;
    emit_ip(n, n->cfg.gw_ip, bcast_ip, 17, udp, 8 + blen, 1);
    if (!n->dhcp_logged) {
        uint8_t *d = (uint8_t *)&n->cfg.dns_ip;
        nlog(n, "dhcp %s, dns %u.%u.%u.%u", mtype == 2 ? "offer" : "ack", d[0], d[1], d[2], d[3]);
        n->dhcp_logged = 1;
    }
}
static void dhcp_input(wfcnet *n, const uint8_t *udp, int len) {
    if (len < 8 + 240) return;
    const uint8_t *b = udp + 8;
    int blen = len - 8;
    if (b[0] != 1 || b[236] != 0x63 || b[237] != 0x82 || b[238] != 0x53 || b[239] != 0x63) return;
    int msg = dhcp_msg(b + 240, blen - 240);
    if (msg == 1) dhcp_reply(n, b, 2);
    else if (msg == 3 || msg == 8) dhcp_reply(n, b, 5);
    if (!n->have_mac && b[2] == 6) { memcpy(n->ds_mac, b + 28, 6); n->have_mac = 1; }
}
static void arp_input(wfcnet *n, const uint8_t src[6], const uint8_t *p, int len) {
    if (len < 28 || rd16(p) != 1 || rd16(p + 2) != 0x0800 || p[4] != 6 || p[5] != 4) return;
    if (rd16(p + 6) != 1) return;
    uint8_t reply[28];
    memset(reply, 0, sizeof reply);
    put16(reply, 1); put16(reply + 2, 0x0800); reply[4] = 6; reply[5] = 4; put16(reply + 6, 2);
    memcpy(reply + 8, n->cfg.gw_mac, 6);
    memcpy(reply + 14, p + 24, 4);                /* the address they asked for, on our MAC */
    memcpy(reply + 18, src, 6);
    memcpy(reply + 24, p + 14, 4);
    inject(n, src, 0x0806, reply, 28);
}
static void icmp_input(wfcnet *n, uint32_t src, uint32_t dst, const uint8_t *p, int len) {
    if (dst != n->cfg.gw_ip || len < 8 || p[0] != 8) return;
    uint8_t *r = malloc(len); if (!r) return;
    memcpy(r, p, len); r[0] = 0; r[2] = r[3] = 0;
    uint16_t c = ip_csum(r, len);
    r[2] = c >> 8; r[3] = c & 0xff;
    emit_ip(n, n->cfg.gw_ip, src ? src : n->cfg.ds_ip, 1, r, len, 0);
    free(r);
}

static void ipv4_input(wfcnet *n, const uint8_t *p, int len) {
    if (len < 20 || (p[0] >> 4) != 4) return;
    int ihl = (p[0] & 0xf) * 4;
    if (ihl < 20 || ihl > len) return;
    if (((p[6] & 0x1f) << 8) | p[7]) return;      /* a fragment */
    int tot = (p[2] << 8) | p[3];
    if (tot < ihl || tot > len) tot = len;
    uint32_t src, dst; memcpy(&src, p + 12, 4); memcpy(&dst, p + 16, 4);
    int proto = p[9];
    const uint8_t *l4 = p + ihl;
    int l4len = tot - ihl;
    if (proto == 17 && l4len >= 4 && rd16(l4 + 2) == 67) { dhcp_input(n, l4, l4len); return; }
    if (proto == 1) { icmp_input(n, src, dst, l4, l4len); return; }
    if (proto == 17) udp_input(n, dst, l4, l4len);
    else if (proto == 6) tcp_input(n, dst, l4, l4len);
}

wfcnet *wfcnet_create(const wfcnet_cfg *cfg) {
    wfcnet *n = calloc(1, sizeof *n);
    if (!n) return 0;
    n->cfg = *cfg;
    for (int i = 0; i < UDP_N; i++) n->udp[i].fd = -1;
    for (int i = 0; i < TCP_N; i++) n->tcp[i].fd = -1;
    return n;
}
void wfcnet_reset(wfcnet *n) {
    if (!n) return;
    for (int i = 0; i < UDP_N; i++) udp_close(&n->udp[i]);
    for (int i = 0; i < TCP_N; i++) if (n->tcp[i].fd >= 0) tcp_free(&n->tcp[i]);
    n->dhcp_logged = 0;
}
void wfcnet_destroy(wfcnet *n) { if (!n) return; wfcnet_reset(n); free(n); }
void wfcnet_note_mac(wfcnet *n, const uint8_t mac[6]) {
    if (!mac) return;
    memcpy(n->ds_mac, mac, 6); n->have_mac = 1;
}
void wfcnet_input(wfcnet *n, const uint8_t dst[6], const uint8_t src[6], uint16_t ethertype,
                  const uint8_t *payload, int len) {
    (void)dst;
    if (!n || !payload || len < 0) return;
    if (!n->have_mac && memcmp(src, bcast, 6)) wfcnet_note_mac(n, src);
    if (ethertype == 0x0806) arp_input(n, src, payload, len);
    else if (ethertype == 0x0800) ipv4_input(n, payload, len);
}
void wfcnet_poll(wfcnet *n) {
    if (!n) return;
    for (int i = 0; i < UDP_N; i++) if (n->udp[i].fd >= 0) udp_poll(n, &n->udp[i]);
    for (int i = 0; i < TCP_N; i++) if (n->tcp[i].fd >= 0) tcp_poll(n, &n->tcp[i]);
}
