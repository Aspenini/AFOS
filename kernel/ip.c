#include "ip.h"
#include "ethernet.h"
#include "arp.h"
#include "types.h"

// Forward declarations
void terminal_writestring(const char* data);
void terminal_writestring_color(const char* data, uint8_t color);

#define COLOR_RED 0x0C
#define COLOR_GREEN 0x0A

// Our IP address (from ARP layer)
static uint32_t our_ip = 0;

// Calculate IP checksum
uint16_t ip_checksum(const void* data, uint16_t length) {
    const uint16_t* words = (const uint16_t*)data;
    uint32_t sum = 0;
    
    // Sum all 16-bit words
    for (uint16_t i = 0; i < length / 2; i++) {
        uint16_t word = ((words[i] & 0xFF) << 8) | ((words[i] >> 8) & 0xFF);
        sum += word;
    }
    
    // Handle odd byte
    if (length % 2) {
        uint8_t byte = ((const uint8_t*)data)[length - 1];
        sum += byte << 8;
    }
    
    // Fold carries
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    // One's complement
    return ~sum;
}

// Initialize IP layer
void ip_init(void) {
    our_ip = arp_get_ip();
    terminal_writestring("IP layer initialized\n");
}

// Send IP packet
int ip_send_packet(uint32_t dst_ip, uint8_t protocol, const uint8_t* data, uint16_t data_len) {
    if (data == NULL || data_len == 0) {
        return -1;
    }
    
    // Build IP header
    ip_header_t ip;
    ip.version_ihl = (IP_VERSION << 4) | (IP_HEADER_SIZE / 4);
    ip.tos = 0;
    ip.total_length = ((IP_HEADER_SIZE + data_len) & 0xFF) << 8 | ((IP_HEADER_SIZE + data_len) >> 8);
    ip.id = 0;  // Simple implementation, no fragmentation
    ip.flags_fragment = 0;
    ip.ttl = IP_DEFAULT_TTL;
    ip.protocol = protocol;
    ip.checksum = 0;
    // IP addresses in header are in network byte order (big-endian)
    // Convert from host byte order (little-endian on x86)
    ip.src_ip = ((our_ip & 0xFF) << 24) | ((our_ip & 0xFF00) << 8) | ((our_ip & 0xFF0000) >> 8) | ((our_ip & 0xFF000000) >> 24);
    ip.dst_ip = ((dst_ip & 0xFF) << 24) | ((dst_ip & 0xFF00) << 8) | ((dst_ip & 0xFF0000) >> 8) | ((dst_ip & 0xFF000000) >> 24);
    
    // Calculate checksum
    ip.checksum = ip_checksum(&ip, IP_HEADER_SIZE);
    
    // Lookup destination MAC address
    uint8_t dst_mac[6];
    if (arp_lookup(dst_ip, dst_mac) != 0) {
        // MAC not in cache, try to resolve it
        extern int arp_resolve(uint32_t ip, uint8_t* mac, int timeout_ms);
        if (arp_resolve(dst_ip, dst_mac, 2000) != 0) {
            // ARP resolution failed (timeout or error)
            // This is expected for first ping - gateway might not respond immediately
            // For now, we'll use broadcast MAC as fallback for gateway
            // In QEMU user networking, gateway is typically at 10.0.2.2
            // For remote IPs, we should use gateway MAC, but for simplicity, try broadcast
            for (int i = 0; i < 6; i++) {
                dst_mac[i] = 0xFF;  // Broadcast - will work for same subnet
            }
        }
    }
    
    // Build full packet (header + data)
    uint8_t packet[1500];
    ip_header_t* ip_ptr = (ip_header_t*)packet;
    *ip_ptr = ip;
    
    for (uint16_t i = 0; i < data_len; i++) {
        packet[IP_HEADER_SIZE + i] = data[i];
    }
    
    // Send via Ethernet
    return ethernet_send_frame(dst_mac, ETH_TYPE_IPV4, packet, IP_HEADER_SIZE + data_len);
}

// Process received IP packet
void ip_process_packet(const uint8_t* data, uint16_t length, const uint8_t* src_mac) {
    if (data == NULL || length < IP_HEADER_SIZE) {
        return;
    }
    
    ip_header_t* ip = (ip_header_t*)data;
    
    // Verify version
    if ((ip->version_ihl >> 4) != IP_VERSION) {
        return;
    }
    
    // Verify checksum
    uint16_t received_checksum = ip->checksum;
    ip->checksum = 0;
    uint16_t calculated_checksum = ip_checksum(ip, IP_HEADER_SIZE);
    if (received_checksum != calculated_checksum) {
        return;  // Checksum mismatch
    }
    ip->checksum = received_checksum;  // Restore
    
    // Convert from network byte order
    uint16_t total_length = ((ip->total_length & 0xFF) << 8) | ((ip->total_length >> 8) & 0xFF);
    
    // Verify packet length
    if (total_length > length || total_length < IP_HEADER_SIZE) {
        return;
    }
    
    // Convert IP addresses from network byte order
    uint32_t src_ip_net = ip->src_ip;
    uint32_t dst_ip_net = ip->dst_ip;
    uint32_t src_ip = ((src_ip_net & 0xFF) << 24) | ((src_ip_net & 0xFF00) << 8) | ((src_ip_net & 0xFF0000) >> 8) | ((src_ip_net & 0xFF000000) >> 24);
    uint32_t dst_ip = ((dst_ip_net & 0xFF) << 24) | ((dst_ip_net & 0xFF00) << 8) | ((dst_ip_net & 0xFF0000) >> 8) | ((dst_ip_net & 0xFF000000) >> 24);
    
    // Check if packet is for us
    if (dst_ip != our_ip) {
        return;  // Not for us, ignore
    }
    
    // Extract payload
    const uint8_t* payload = data + IP_HEADER_SIZE;
    uint16_t payload_len = total_length - IP_HEADER_SIZE;
    
    // Add sender to ARP cache
    arp_add_entry(src_ip, src_mac);
    
    // Route to protocol handler
    switch (ip->protocol) {
        case IP_PROTO_ICMP:
            // Handle ICMP packets
            extern void icmp_process_packet(const uint8_t* data, uint16_t length, uint32_t src_ip);
            icmp_process_packet(payload, payload_len, src_ip);
            break;
            
        case IP_PROTO_TCP:
            // TCP not implemented yet
            break;
            
        case IP_PROTO_UDP:
            // UDP not implemented yet
            break;
            
        default:
            // Unknown protocol, ignore
            break;
    }
}

// Get our IP address
uint32_t ip_get_address(void) {
    return our_ip;
}

// Set our IP address
void ip_set_address(uint32_t ip) {
    our_ip = ip;
    arp_set_ip(ip);
}

