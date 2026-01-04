#include "icmp.h"
#include "ip.h"
#include "types.h"

// Forward declarations
void terminal_writestring(const char* data);
void terminal_writestring_color(const char* data, uint8_t color);

#define COLOR_RED 0x0C
#define COLOR_GREEN 0x0A

// Calculate ICMP checksum
uint16_t icmp_checksum(const void* data, uint16_t length) {
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

// Initialize ICMP layer
void icmp_init(void) {
    terminal_writestring("ICMP layer initialized\n");
}

// Send ICMP echo request (ping)
int icmp_send_echo_request(uint32_t dst_ip, uint16_t id, uint16_t sequence, const uint8_t* data, uint16_t data_len) {
    // Build ICMP header
    icmp_header_t icmp;
    icmp.type = ICMP_TYPE_ECHO_REQUEST;
    icmp.code = 0;
    icmp.checksum = 0;
    icmp.id = ((id & 0xFF) << 8) | ((id >> 8) & 0xFF);
    icmp.sequence = ((sequence & 0xFF) << 8) | ((sequence >> 8) & 0xFF);
    
    // Build packet (header + data)
    uint8_t packet[1500];
    icmp_header_t* icmp_ptr = (icmp_header_t*)packet;
    *icmp_ptr = icmp;
    
    // Copy data if provided
    if (data != NULL && data_len > 0) {
        for (uint16_t i = 0; i < data_len && i < (1500 - ICMP_HEADER_SIZE); i++) {
            packet[ICMP_HEADER_SIZE + i] = data[i];
        }
    } else {
        // Default ping data (timestamp-like)
        for (uint16_t i = 0; i < 32; i++) {
            packet[ICMP_HEADER_SIZE + i] = (uint8_t)i;
        }
        data_len = 32;
    }
    
    // Calculate checksum
    icmp_ptr->checksum = icmp_checksum(packet, ICMP_HEADER_SIZE + data_len);
    
    // Send via IP layer
    return ip_send_packet(dst_ip, IP_PROTO_ICMP, packet, ICMP_HEADER_SIZE + data_len);
}

// Process received ICMP packet
void icmp_process_packet(const uint8_t* data, uint16_t length, uint32_t src_ip) {
    if (data == NULL || length < ICMP_HEADER_SIZE) {
        return;
    }
    
    icmp_header_t* icmp = (icmp_header_t*)data;
    
    // Verify checksum
    uint16_t received_checksum = icmp->checksum;
    icmp->checksum = 0;
    uint16_t calculated_checksum = icmp_checksum(data, length);
    if (received_checksum != calculated_checksum) {
        return;  // Checksum mismatch
    }
    icmp->checksum = received_checksum;  // Restore
    
    // Handle echo request
    if (icmp->type == ICMP_TYPE_ECHO_REQUEST) {
        // Build echo reply
        icmp_header_t reply;
        reply.type = ICMP_TYPE_ECHO_REPLY;
        reply.code = 0;
        reply.checksum = 0;
        reply.id = icmp->id;  // Same ID
        reply.sequence = icmp->sequence;  // Same sequence
        
        // Build reply packet
        uint8_t reply_packet[1500];
        icmp_header_t* reply_ptr = (icmp_header_t*)reply_packet;
        *reply_ptr = reply;
        
        // Copy original data
        uint16_t data_len = length - ICMP_HEADER_SIZE;
        for (uint16_t i = 0; i < data_len; i++) {
            reply_packet[ICMP_HEADER_SIZE + i] = data[ICMP_HEADER_SIZE + i];
        }
        
        // Calculate checksum
        reply_ptr->checksum = icmp_checksum(reply_packet, ICMP_HEADER_SIZE + data_len);
        
        // Send reply via IP layer
        ip_send_packet(src_ip, IP_PROTO_ICMP, reply_packet, ICMP_HEADER_SIZE + data_len);
        
        // Print ping received message
        terminal_writestring_color("Ping received from ", COLOR_GREEN);
        // Print IP address (simple format)
        char ip_str[16];
        uint32_t ip = src_ip;
        int pos = 0;
        for (int i = 0; i < 4; i++) {
            if (i > 0) ip_str[pos++] = '.';
            uint8_t octet = (ip >> (i * 8)) & 0xFF;
            if (octet == 0) {
                ip_str[pos++] = '0';
            } else {
                char num[4];
                int num_pos = 0;
                int temp = octet;
                if (temp == 0) {
                    num[num_pos++] = '0';
                } else {
                    char rev[4];
                    int rev_pos = 0;
                    while (temp > 0) {
                        rev[rev_pos++] = '0' + (temp % 10);
                        temp /= 10;
                    }
                    for (int j = rev_pos - 1; j >= 0; j--) {
                        num[num_pos++] = rev[j];
                    }
                }
                num[num_pos] = '\0';
                for (int j = 0; j < num_pos; j++) {
                    ip_str[pos++] = num[j];
                }
            }
        }
        ip_str[pos] = '\0';
        terminal_writestring(ip_str);
        terminal_writestring("\n");
    }
    
    // Handle echo reply (reply to our ping)
    if (icmp->type == ICMP_TYPE_ECHO_REPLY) {
        // Extract ID and sequence (network byte order)
        uint16_t id = ((icmp->id & 0xFF) << 8) | ((icmp->id >> 8) & 0xFF);
        uint16_t sequence = ((icmp->sequence & 0xFF) << 8) | ((icmp->sequence >> 8) & 0xFF);
        
        // Print ping reply message
        terminal_writestring_color("Ping reply from ", COLOR_GREEN);
        // Print IP address (simple format)
        char ip_str[16];
        uint32_t ip = src_ip;
        int pos = 0;
        for (int i = 0; i < 4; i++) {
            if (i > 0) ip_str[pos++] = '.';
            uint8_t octet = (ip >> (i * 8)) & 0xFF;
            if (octet == 0) {
                ip_str[pos++] = '0';
            } else {
                char num[4];
                int num_pos = 0;
                int temp = octet;
                if (temp == 0) {
                    num[num_pos++] = '0';
                } else {
                    char rev[4];
                    int rev_pos = 0;
                    while (temp > 0) {
                        rev[rev_pos++] = '0' + (temp % 10);
                        temp /= 10;
                    }
                    for (int j = rev_pos - 1; j >= 0; j--) {
                        num[num_pos++] = rev[j];
                    }
                }
                num[num_pos] = '\0';
                for (int j = 0; j < num_pos; j++) {
                    ip_str[pos++] = num[j];
                }
            }
        }
        ip_str[pos] = '\0';
        terminal_writestring(ip_str);
        terminal_writestring(": seq=");
        // Print sequence number
        char seq_str[4];
        int seq_pos = 0;
        int temp = sequence;
        if (temp == 0) {
            seq_str[seq_pos++] = '0';
        } else {
            char rev[4];
            int rev_pos = 0;
            while (temp > 0) {
                rev[rev_pos++] = '0' + (temp % 10);
                temp /= 10;
            }
            for (int j = rev_pos - 1; j >= 0; j--) {
                seq_str[seq_pos++] = rev[j];
            }
        }
        seq_str[seq_pos] = '\0';
        terminal_writestring(seq_str);
        terminal_writestring("\n");
    }
}

