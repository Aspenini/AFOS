#include "arp.h"
#include "ethernet.h"
#include "types.h"

// Forward declarations
void terminal_writestring(const char* data);
void terminal_writestring_color(const char* data, uint8_t color);

#define COLOR_RED 0x0C
#define COLOR_GREEN 0x0A

// Our IP address (will be configurable later)
// QEMU user networking: guest typically gets 10.0.2.15, host is 10.0.2.2
static uint32_t our_ip = 0x0A00020F;  // 10.0.2.15 (QEMU user networking default)

// ARP cache
static arp_entry_t arp_cache[ARP_CACHE_SIZE];
static int arp_cache_count = 0;

// Broadcast MAC address
static const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Simple memset implementation
static void* memset(void* s, int c, size_t n) {
    uint8_t* p = (uint8_t*)s;
    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)c;
    }
    return s;
}

// Simple memcmp implementation
static int memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* p1 = (const uint8_t*)s1;
    const uint8_t* p2 = (const uint8_t*)s2;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] < p2[i]) return -1;
        if (p1[i] > p2[i]) return 1;
    }
    return 0;
}

// Initialize ARP layer
void arp_init(void) {
    memset(arp_cache, 0, sizeof(arp_cache));
    arp_cache_count = 0;
    terminal_writestring("ARP layer initialized\n");
}

// Send ARP request
int arp_send_request(uint32_t target_ip) {
    arp_packet_t arp;
    
    // Build ARP request packet
    arp.htype = ((ARP_HTYPE_ETHERNET & 0xFF) << 8) | ((ARP_HTYPE_ETHERNET >> 8) & 0xFF);
    arp.ptype = ((ARP_PTYPE_IPV4 & 0xFF) << 8) | ((ARP_PTYPE_IPV4 >> 8) & 0xFF);
    arp.hlen = 6;
    arp.plen = 4;
    arp.op = ((ARP_OP_REQUEST & 0xFF) << 8) | ((ARP_OP_REQUEST >> 8) & 0xFF);
    
    // Get our MAC address
    uint8_t our_mac[6];
    ethernet_get_mac(our_mac);
    
    // Sender (us)
    for (int i = 0; i < 6; i++) {
        arp.sha[i] = our_mac[i];
    }
    arp.spa = our_ip;
    
    // Target (unknown MAC, known IP)
    for (int i = 0; i < 6; i++) {
        arp.tha[i] = 0;
    }
    arp.tpa = target_ip;
    
    // Send via Ethernet (broadcast)
    return ethernet_send_frame(broadcast_mac, ETH_TYPE_ARP, (uint8_t*)&arp, sizeof(arp_packet_t));
}

// Send ARP reply
int arp_send_reply(const uint8_t* target_mac, uint32_t target_ip, uint32_t sender_ip) {
    if (target_mac == NULL) {
        return -1;
    }
    
    arp_packet_t arp;
    
    // Build ARP reply packet
    arp.htype = ((ARP_HTYPE_ETHERNET & 0xFF) << 8) | ((ARP_HTYPE_ETHERNET >> 8) & 0xFF);
    arp.ptype = ((ARP_PTYPE_IPV4 & 0xFF) << 8) | ((ARP_PTYPE_IPV4 >> 8) & 0xFF);
    arp.hlen = 6;
    arp.plen = 4;
    arp.op = ((ARP_OP_REPLY & 0xFF) << 8) | ((ARP_OP_REPLY >> 8) & 0xFF);
    
    // Get our MAC address
    uint8_t our_mac[6];
    ethernet_get_mac(our_mac);
    
    // Sender (us)
    for (int i = 0; i < 6; i++) {
        arp.sha[i] = our_mac[i];
    }
    arp.spa = sender_ip;
    
    // Target (requestor)
    for (int i = 0; i < 6; i++) {
        arp.tha[i] = target_mac[i];
    }
    arp.tpa = target_ip;
    
    // Send via Ethernet
    return ethernet_send_frame(target_mac, ETH_TYPE_ARP, (uint8_t*)&arp, sizeof(arp_packet_t));
}

// Process received ARP packet
void arp_process_packet(const uint8_t* data, uint16_t length, const uint8_t* src_mac) {
    if (data == NULL || length < sizeof(arp_packet_t)) {
        return;
    }
    
    arp_packet_t* arp = (arp_packet_t*)data;
    
    // Convert from network byte order
    uint16_t htype = ((arp->htype & 0xFF) << 8) | ((arp->htype >> 8) & 0xFF);
    uint16_t ptype = ((arp->ptype & 0xFF) << 8) | ((arp->ptype >> 8) & 0xFF);
    uint16_t op = ((arp->op & 0xFF) << 8) | ((arp->op >> 8) & 0xFF);
    
    // Validate packet
    if (htype != ARP_HTYPE_ETHERNET || ptype != ARP_PTYPE_IPV4 || arp->hlen != 6 || arp->plen != 4) {
        return;
    }
    
    // Extract IP addresses (network byte order)
    uint32_t sender_ip = arp->spa;
    uint32_t target_ip = arp->tpa;
    
    // Add sender to ARP cache
    arp_add_entry(sender_ip, src_mac);
    
    // Handle ARP request
    if (op == ARP_OP_REQUEST) {
        // Check if request is for us
        if (target_ip == our_ip) {
            // Send ARP reply
            arp_send_reply(src_mac, sender_ip, our_ip);
        }
    }
    // ARP reply is handled by adding to cache above
}

// Lookup MAC address for IP
int arp_lookup(uint32_t ip, uint8_t* mac) {
    if (mac == NULL) {
        return -1;
    }
    
    // Search cache
    for (int i = 0; i < arp_cache_count; i++) {
        if (arp_cache[i].ip == ip) {
            for (int j = 0; j < 6; j++) {
                mac[j] = arp_cache[i].mac[j];
            }
            return 0;
        }
    }
    
    return -1;  // Not found
}

// Add entry to ARP cache
void arp_add_entry(uint32_t ip, const uint8_t* mac) {
    if (mac == NULL) {
        return;
    }
    
    // Check if entry already exists
    for (int i = 0; i < arp_cache_count; i++) {
        if (arp_cache[i].ip == ip) {
            // Update existing entry
            for (int j = 0; j < 6; j++) {
                arp_cache[i].mac[j] = mac[j];
            }
            arp_cache[i].age = 0;
            return;
        }
    }
    
    // Add new entry
    if (arp_cache_count < ARP_CACHE_SIZE) {
        arp_cache[arp_cache_count].ip = ip;
        for (int j = 0; j < 6; j++) {
            arp_cache[arp_cache_count].mac[j] = mac[j];
        }
        arp_cache[arp_cache_count].age = 0;
        arp_cache_count++;
    } else {
        // Cache full, replace oldest entry (simple round-robin)
        static int next_replace = 0;
        arp_cache[next_replace].ip = ip;
        for (int j = 0; j < 6; j++) {
            arp_cache[next_replace].mac[j] = mac[j];
        }
        arp_cache[next_replace].age = 0;
        next_replace = (next_replace + 1) % ARP_CACHE_SIZE;
    }
}

// Set our IP address
void arp_set_ip(uint32_t ip) {
    our_ip = ip;
}

// Get our IP address
uint32_t arp_get_ip(void) {
    return our_ip;
}

// Resolve IP to MAC with timeout (sends ARP request and waits for reply)
int arp_resolve(uint32_t ip, uint8_t* mac, int timeout_ms) {
    if (mac == NULL) {
        return -1;
    }
    
    // Check cache first
    if (arp_lookup(ip, mac) == 0) {
        return 0;  // Already in cache
    }
    
    // Send ARP request
    arp_send_request(ip);
    
    // Poll for ARP reply (with timeout)
    // timeout_ms is approximate - we poll in small increments
    int poll_count = timeout_ms / 10;  // Poll every ~10ms
    if (poll_count < 1) poll_count = 1;
    
    for (int i = 0; i < poll_count; i++) {
        // Poll for network packets
        arp_poll();
        
        // Check cache again
        if (arp_lookup(ip, mac) == 0) {
            return 0;  // Resolved!
        }
        
        // Small delay (approximate 10ms)
        for (volatile int j = 0; j < 10000; j++);
    }
    
    return -1;  // Timeout
}

// Poll for network packets (call this periodically to process received packets)
void arp_poll(void) {
    // Poll RTL8139 for received packets
    uint8_t frame[1514];
    int length;
    
    // Try to receive a few packets
    for (int i = 0; i < 10; i++) {
        extern int ethernet_receive_frame(uint8_t* buffer, uint16_t max_len);
        extern void ethernet_process_frame(const uint8_t* frame, uint16_t length);
        
        length = ethernet_receive_frame(frame, sizeof(frame));
        if (length > 0) {
            ethernet_process_frame(frame, length);
        } else {
            break;  // No more packets
        }
    }
}

