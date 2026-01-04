#ifndef ARP_H
#define ARP_H

#include "types.h"

// ARP operation codes
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2

// ARP hardware types
#define ARP_HTYPE_ETHERNET 1

// ARP protocol types
#define ARP_PTYPE_IPV4 0x0800

// ARP packet structure
typedef struct {
    uint16_t htype;        // Hardware type
    uint16_t ptype;        // Protocol type
    uint8_t  hlen;         // Hardware address length
    uint8_t  plen;         // Protocol address length
    uint16_t op;           // Operation
    uint8_t  sha[6];        // Sender hardware address
    uint32_t spa;           // Sender protocol address
    uint8_t  tha[6];        // Target hardware address
    uint32_t tpa;           // Target protocol address
} __attribute__((packed)) arp_packet_t;

// ARP cache entry
typedef struct {
    uint32_t ip;
    uint8_t mac[6];
    uint32_t age;  // Age counter (for cache expiration)
} arp_entry_t;

#define ARP_CACHE_SIZE 16

// Function prototypes
void arp_init(void);
int arp_send_request(uint32_t target_ip);
int arp_send_reply(const uint8_t* target_mac, uint32_t target_ip, uint32_t sender_ip);
void arp_process_packet(const uint8_t* data, uint16_t length, const uint8_t* src_mac);
int arp_lookup(uint32_t ip, uint8_t* mac);
void arp_add_entry(uint32_t ip, const uint8_t* mac);
void arp_set_ip(uint32_t ip);
uint32_t arp_get_ip(void);
int arp_resolve(uint32_t ip, uint8_t* mac, int timeout_ms);
void arp_poll(void);

#endif // ARP_H

