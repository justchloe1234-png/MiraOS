#pragma once

#include <stdint.h>
#include <stdbool.h>

#define NET_MAX_PACKET 1518
#define NET_ETH_TYPE_IP 0x0800
#define NET_ETH_TYPE_ARP 0x0806

typedef struct {
    uint8_t dst_mac[6];
    uint8_t src_mac[6];
    uint16_t eth_type;
} eth_header_t;

void net_init(void);
void net_send(const void *data, uint16_t size);
bool net_receive(void *buffer, uint16_t *size);
void net_set_mac(const uint8_t *mac);
void net_get_mac(uint8_t *mac);