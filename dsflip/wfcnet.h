/* Userspace NAT in front of one emulated DS. Speaks Ethernet (no 802.11).
 * The DS is 10.13.37.20; the gateway is 10.13.37.1. DHCP option 6 is the DNS server
 * the caller picked, which is what a melonDS "DNS override" changes in the firmware. */
#ifndef WFCNET_H
#define WFCNET_H
#include <stdint.h>

typedef void (*wfcnet_inject_fn)(void *ud, const uint8_t dst[6], const uint8_t src[6],
                                 uint16_t ethertype, const uint8_t *payload, int len);
typedef void (*wfcnet_log_fn)(void *ud, const char *line);

typedef struct wfcnet wfcnet;

typedef struct {
    uint32_t ds_ip, gw_ip, dns_ip;      /* network order */
    uint8_t gw_mac[6];
    wfcnet_inject_fn inject;
    wfcnet_log_fn log;
    void *ud;
    int debug;
} wfcnet_cfg;

wfcnet *wfcnet_create(const wfcnet_cfg *cfg);
void wfcnet_destroy(wfcnet *n);
void wfcnet_reset(wfcnet *n);
void wfcnet_note_mac(wfcnet *n, const uint8_t mac[6]);
/* One Ethernet frame from the DS. ethertype is host order. payload follows the type field. */
void wfcnet_input(wfcnet *n, const uint8_t dst[6], const uint8_t src[6], uint16_t ethertype,
                  const uint8_t *payload, int len);
void wfcnet_poll(wfcnet *n);
#endif
