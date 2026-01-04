#ifndef ETHERNET_H
#define ETHERNET_H

#include "types.h"

// Ethernet frame header
#define ETH_HEADER_SIZE 14
#define ETH_MIN_SIZE 60
#define ETH_MAX_SIZE 1514
#define ETH_DATA_MAX (ETH_MAX_SIZE - ETH_HEADER_SIZE)

// Ethernet type codes
#define ETH_TYPE_IPV4  0x0800
#define ETH_TYPE_ARP   0x0806
#define ETH_TYPE_IPV6  0x86DD

// Ethernet frame structure
typedef struct {
    uint8_t dest_mac[6];    // Destination MAC address
    uint8_t src_mac[6];     // Source MAC address
    uint16_t type;           // EtherType (network protocol)
    uint8_t data[];          // Payload (variable length)
} __attribute__((packed)) ethernet_frame_t;

// Function prototypes
int ethernet_send_frame(const uint8_t* dest_mac, uint16_t type, const uint8_t* data, uint16_t data_len);
int ethernet_receive_frame(uint8_t* buffer, uint16_t max_len);
void ethernet_get_mac(uint8_t* mac);
void ethernet_process_frame(const uint8_t* frame, uint16_t length);
void ethernet_poll_for_packets(void);

#endif // ETHERNET_H

