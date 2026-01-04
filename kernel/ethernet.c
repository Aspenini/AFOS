#include "ethernet.h"
#include "rtl8139.h"
#include "types.h"

// Forward declarations
void terminal_writestring(const char* data);
void terminal_writestring_color(const char* data, uint8_t color);

#define COLOR_RED 0x0C
#define COLOR_GREEN 0x0A

// Our MAC address (from RTL8139)
static uint8_t our_mac[6] = {0};

// Send Ethernet frame
int ethernet_send_frame(const uint8_t* dest_mac, uint16_t type, const uint8_t* data, uint16_t data_len) {
    if (dest_mac == NULL || data == NULL || data_len == 0 || data_len > ETH_DATA_MAX) {
        return -1;
    }
    
    // Allocate buffer for full frame
    uint8_t frame[ETH_MAX_SIZE];
    ethernet_frame_t* eth = (ethernet_frame_t*)frame;
    
    // Build Ethernet header
    for (int i = 0; i < 6; i++) {
        eth->dest_mac[i] = dest_mac[i];
        eth->src_mac[i] = our_mac[i];
    }
    
    // Convert type to network byte order (big-endian)
    eth->type = ((type & 0xFF) << 8) | ((type >> 8) & 0xFF);
    
    // Copy payload
    for (uint16_t i = 0; i < data_len; i++) {
        eth->data[i] = data[i];
    }
    
    // Calculate total frame size (header + data)
    uint16_t frame_size = ETH_HEADER_SIZE + data_len;
    
    // Pad to minimum frame size (60 bytes) if needed
    if (frame_size < ETH_MIN_SIZE) {
        for (uint16_t i = frame_size; i < ETH_MIN_SIZE; i++) {
            frame[i] = 0;
        }
        frame_size = ETH_MIN_SIZE;
    }
    
    // Send via RTL8139
    return rtl8139_send_packet(frame, frame_size);
}

// Receive Ethernet frame
int ethernet_receive_frame(uint8_t* buffer, uint16_t max_len) {
    if (buffer == NULL || max_len < ETH_HEADER_SIZE) {
        return -1;
    }
    
    // Receive from RTL8139
    uint8_t packet[ETH_MAX_SIZE];
    int length = rtl8139_receive_packet(packet, ETH_MAX_SIZE);
    
    if (length < 0) {
        return -1;
    }
    
    // Check minimum frame size
    if (length < ETH_HEADER_SIZE) {
        return -1;
    }
    
    // Check if frame is for us (or broadcast)
    ethernet_frame_t* eth = (ethernet_frame_t*)packet;
    int is_broadcast = 1;
    int is_for_us = 1;
    
    for (int i = 0; i < 6; i++) {
        if (eth->dest_mac[i] != 0xFF) {
            is_broadcast = 0;
        }
        if (eth->dest_mac[i] != our_mac[i]) {
            is_for_us = 0;
        }
    }
    
    // Only accept frames addressed to us or broadcast
    if (!is_broadcast && !is_for_us) {
        return -1;
    }
    
    // Copy frame to buffer
    uint16_t copy_len = (length < max_len) ? length : max_len;
    for (uint16_t i = 0; i < copy_len; i++) {
        buffer[i] = packet[i];
    }
    
    return copy_len;
}

// Get our MAC address
void ethernet_get_mac(uint8_t* mac) {
    if (mac == NULL) return;
    for (int i = 0; i < 6; i++) {
        mac[i] = our_mac[i];
    }
}

// Process received Ethernet frame (called by network stack)
void ethernet_process_frame(const uint8_t* frame, uint16_t length) {
    if (frame == NULL || length < ETH_HEADER_SIZE) {
        return;
    }
    
    ethernet_frame_t* eth = (ethernet_frame_t*)frame;
    
    // Convert type from network byte order
    uint16_t type = ((eth->type & 0xFF) << 8) | ((eth->type >> 8) & 0xFF);
    
    // Extract payload
    const uint8_t* payload = frame + ETH_HEADER_SIZE;
    uint16_t payload_len = length - ETH_HEADER_SIZE;
    
    // Route to appropriate protocol handler
    switch (type) {
        case ETH_TYPE_ARP:
            // Handle ARP packets
            extern void arp_process_packet(const uint8_t* data, uint16_t length, const uint8_t* src_mac);
            arp_process_packet(payload, payload_len, eth->src_mac);
            break;
            
        case ETH_TYPE_IPV4:
            // Handle IP packets
            extern void ip_process_packet(const uint8_t* data, uint16_t length, const uint8_t* src_mac);
            ip_process_packet(payload, payload_len, eth->src_mac);
            break;
            
        default:
            // Unknown protocol, ignore
            break;
    }
}

// Poll for received packets (call this periodically to process incoming frames)
void ethernet_poll_for_packets(void) {
    uint8_t frame[ETH_MAX_SIZE];
    
    // Try to receive multiple packets (up to 10 to avoid infinite loop)
    for (int i = 0; i < 10; i++) {
        int length = ethernet_receive_frame(frame, ETH_MAX_SIZE);
        
        if (length > 0) {
            ethernet_process_frame(frame, length);
        } else {
            // No more packets
            break;
        }
    }
}

// Initialize Ethernet layer
void ethernet_init(void) {
    // Get MAC address from RTL8139
    rtl8139_get_mac(our_mac);
    
    terminal_writestring("Ethernet layer initialized\n");
}

