#ifndef IP_H
#define IP_H

#include "types.h"

// IP protocol numbers
#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP  6
#define IP_PROTO_UDP  17

// IP header structure
typedef struct {
    uint8_t  version_ihl;    // Version (4 bits) + IHL (4 bits)
    uint8_t  tos;            // Type of service
    uint16_t total_length;   // Total packet length
    uint16_t id;             // Identification
    uint16_t flags_fragment; // Flags (3 bits) + Fragment offset (13 bits)
    uint8_t  ttl;            // Time to live
    uint8_t  protocol;       // Protocol
    uint16_t checksum;       // Header checksum
    uint32_t src_ip;         // Source IP address
    uint32_t dst_ip;         // Destination IP address
} __attribute__((packed)) ip_header_t;

#define IP_HEADER_SIZE 20
#define IP_VERSION 4
#define IP_DEFAULT_TTL 64

// Function prototypes
void ip_init(void);
int ip_send_packet(uint32_t dst_ip, uint8_t protocol, const uint8_t* data, uint16_t data_len);
void ip_process_packet(const uint8_t* data, uint16_t length, const uint8_t* src_mac);
uint32_t ip_get_address(void);
void ip_set_address(uint32_t ip);
uint16_t ip_checksum(const void* data, uint16_t length);

#endif // IP_H

