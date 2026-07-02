#include "net.h"
#include "kernel.h"
#include "kernel/heap.h"

static uint8_t net_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
static bool net_initialized = false;

void net_init(void) {
    net_initialized = true;
}

void net_send(const void *data, uint16_t size) {
    if (!net_initialized || !data || size == 0)
        return;
    
    /* In a real implementation, this would send via NIC */
    /* For now, just a stub */
}

bool net_receive(void *buffer, uint16_t *size) {
    if (!net_initialized || !buffer || !size)
        return false;
    
    /* In a real implementation, this would receive from NIC */
    /* For now, just a stub */
    *size = 0;
    return false;
}

void net_set_mac(const uint8_t *mac) {
    if (mac) {
        for (int i = 0; i < 6; i++)
            net_mac[i] = mac[i];
    }
}

void net_get_mac(uint8_t *mac) {
    if (mac) {
        for (int i = 0; i < 6; i++)
            mac[i] = net_mac[i];
    }
}