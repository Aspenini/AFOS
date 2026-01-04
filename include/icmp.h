#ifndef ICMP_H
#define ICMP_H

#include "types.h"

// ICMP message types
#define ICMP_TYPE_ECHO_REPLY   0
#define ICMP_TYPE_ECHO_REQUEST 8

// ICMP header structure
typedef struct {
    uint8_t  type;      // Message type
    uint8_t  code;      // Message code
    uint16_t checksum;  // Checksum
    uint16_t id;        // Identifier (for echo)
    uint16_t sequence; // Sequence number (for echo)
    uint8_t  data[];    // Payload
} __attribute__((packed)) icmp_header_t;

#define ICMP_HEADER_SIZE 8

// Function prototypes
void icmp_init(void);
int icmp_send_echo_request(uint32_t dst_ip, uint16_t id, uint16_t sequence, const uint8_t* data, uint16_t data_len);
void icmp_process_packet(const uint8_t* data, uint16_t length, uint32_t src_ip);
uint16_t icmp_checksum(const void* data, uint16_t length);

#endif // ICMP_H

