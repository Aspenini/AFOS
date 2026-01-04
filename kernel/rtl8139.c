#include "rtl8139.h"
#include "ethernet.h"
#include "types.h"

// Forward declaration for malloc
void* malloc(uint32_t size);

// Forward declarations
void terminal_writestring(const char* data);
void terminal_writestring_color(const char* data, uint8_t color);

#define COLOR_RED 0x0C
#define COLOR_GREEN 0x0A
#define COLOR_YELLOW 0x0E

// Global driver instance
rtl8139_t g_rtl8139 = {0};

// I/O port helpers
static inline uint8_t inb(uint16_t port) {
    uint8_t result;
    __asm__ volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t result;
    __asm__ volatile("inw %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t result;
    __asm__ volatile("inl %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

// PCI configuration space access
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

static uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static void pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

// Find RTL8139 PCI device
static int pci_find_rtl8139(uint8_t* bus, uint8_t* slot, uint8_t* func) {
    // Scan PCI buses (0-255)
    for (uint8_t b = 0; b < 255; b++) {
        // Scan slots (0-31)
        for (uint8_t s = 0; s < 32; s++) {
            // Check function 0
            uint32_t vendor_device = pci_read_config(b, s, 0, 0);
            uint16_t vendor = vendor_device & 0xFFFF;
            uint16_t device = (vendor_device >> 16) & 0xFFFF;
            
            if (vendor == RTL8139_VENDOR_ID && device == RTL8139_DEVICE_ID) {
                *bus = b;
                *slot = s;
                *func = 0;
                return 0;
            }
        }
    }
    return -1;
}

// Read MAC address from RTL8139
static void rtl8139_read_mac(uint16_t io_base, uint8_t* mac) {
    // MAC address is stored in IDR0-IDR5 registers
    for (int i = 0; i < 6; i++) {
        mac[i] = inb(io_base + RTL8139_IDR0 + i);
    }
}

// Initialize RTL8139
int rtl8139_init(void) {
    if (g_rtl8139.initialized) {
        return 0;
    }
    
    terminal_writestring("Searching for RTL8139 network card...\n");
    
    // Find PCI device
    uint8_t bus, slot, func;
    if (pci_find_rtl8139(&bus, &slot, &func) != 0) {
        terminal_writestring_color("RTL8139: Device not found\n", COLOR_RED);
        return -1;
    }
    
    terminal_writestring("RTL8139 found on PCI bus ");
    // Simple number printing
    char bus_str[4];
    int temp = bus;
    int pos = 0;
    if (temp == 0) {
        bus_str[pos++] = '0';
    } else {
        char rev[4];
        int rev_pos = 0;
        while (temp > 0) {
            rev[rev_pos++] = '0' + (temp % 10);
            temp /= 10;
        }
        for (int j = rev_pos - 1; j >= 0; j--) {
            bus_str[pos++] = rev[j];
        }
    }
    bus_str[pos] = '\0';
    terminal_writestring(bus_str);
    terminal_writestring("\n");
    
    // Read I/O base address from BAR0
    uint32_t bar0 = pci_read_config(bus, slot, func, 0x10);
    if ((bar0 & 0x01) == 0) {
        terminal_writestring_color("RTL8139: Not an I/O space BAR\n", COLOR_RED);
        return -1;
    }
    
    uint16_t io_base = bar0 & 0xFFFC;
    g_rtl8139.io_base = io_base;
    
    // Enable bus mastering and I/O space
    uint32_t command = pci_read_config(bus, slot, func, 0x04);
    command |= 0x05;  // Enable I/O space and bus mastering
    pci_write_config(bus, slot, func, 0x04, command);
    
    // Reset the card
    outb(io_base + RTL8139_CR, RTL8139_CR_RST);
    
    // Wait for reset to complete (should clear RST bit)
    int timeout = 1000;
    while ((inb(io_base + RTL8139_CR) & RTL8139_CR_RST) && timeout > 0) {
        for (volatile int i = 0; i < 1000; i++);
        timeout--;
    }
    
    if (timeout == 0) {
        terminal_writestring_color("RTL8139: Reset timeout\n", COLOR_RED);
        return -1;
    }
    
    // Allocate receive buffer (must be 256-byte aligned)
    g_rtl8139.rx_buffer = malloc(RTL8139_RX_BUF_SIZE + 256);
    if (g_rtl8139.rx_buffer == NULL) {
        terminal_writestring_color("RTL8139: Failed to allocate RX buffer\n", COLOR_RED);
        return -1;
    }
    
    // Align to 256 bytes
    uint32_t rx_align = (uint32_t)g_rtl8139.rx_buffer;
    if (rx_align % 256 != 0) {
        rx_align = (rx_align + 256) & ~0xFF;
        g_rtl8139.rx_buffer = (uint8_t*)rx_align;
    }
    g_rtl8139.rx_buffer_phys = (uint32_t)g_rtl8139.rx_buffer;  // Identity mapping for now
    
    // Allocate transmit buffers (must be 16-byte aligned)
    for (int i = 0; i < 4; i++) {
        g_rtl8139.tx_buffer[i] = malloc(RTL8139_TX_BUF_SIZE + 16);
        if (g_rtl8139.tx_buffer[i] == NULL) {
            terminal_writestring_color("RTL8139: Failed to allocate TX buffer\n", COLOR_RED);
            return -1;
        }
        
        // Align to 16 bytes
        uint32_t tx_align = (uint32_t)g_rtl8139.tx_buffer[i];
        if (tx_align % 16 != 0) {
            tx_align = (tx_align + 16) & ~0xF;
            g_rtl8139.tx_buffer[i] = (uint8_t*)tx_align;
        }
        g_rtl8139.tx_buffer_phys[i] = (uint32_t)g_rtl8139.tx_buffer[i];
    }
    
    // Set receive buffer address
    outl(io_base + RTL8139_RXBUF, g_rtl8139.rx_buffer_phys);
    
    // Configure receive: accept all packets, wrap mode
    outl(io_base + RTL8139_RCR, RTL8139_RCR_AAP | RTL8139_RCR_APM | RTL8139_RCR_AM | RTL8139_RCR_AB | RTL8139_RCR_WRAP);
    
    // Configure transmit
    outl(io_base + RTL8139_TCR, 0x03000700);  // Default transmit config
    
    // Clear interrupt status
    outw(io_base + RTL8139_ISR, 0xFFFF);
    
    // Enable interrupts: receive OK, transmit OK
    outw(io_base + RTL8139_IMR, RTL8139_ISR_ROK | RTL8139_ISR_TOK);
    
    // Enable receiver and transmitter
    outb(io_base + RTL8139_CR, RTL8139_CR_RE | RTL8139_CR_TE);
    
    // Read MAC address
    rtl8139_read_mac(io_base, g_rtl8139.mac_address);
    
    g_rtl8139.current_tx = 0;
    g_rtl8139.initialized = 1;
    
    terminal_writestring_color("RTL8139 initialized successfully\n", COLOR_GREEN);
    terminal_writestring("MAC address: ");
    // Print MAC address
    for (int i = 0; i < 6; i++) {
        if (i > 0) terminal_writestring(":");
        uint8_t b = g_rtl8139.mac_address[i];
        char hex[3];
        hex[0] = "0123456789ABCDEF"[(b >> 4) & 0xF];
        hex[1] = "0123456789ABCDEF"[b & 0xF];
        hex[2] = '\0';
        terminal_writestring(hex);
    }
    terminal_writestring("\n");
    
    return 0;
}

// Send packet
int rtl8139_send_packet(uint8_t* data, uint16_t length) {
    if (!g_rtl8139.initialized || data == NULL || length == 0 || length > RTL8139_TX_BUF_SIZE) {
        return -1;
    }
    
    uint16_t io_base = g_rtl8139.io_base;
    uint8_t tx_desc = g_rtl8139.current_tx;
    
    // Wait for previous transmission to complete (check if buffer is available)
    // Read TX status to see if previous packet is done
    uint32_t tx_status = inl(io_base + RTL8139_TXSTATUS0 + (tx_desc * 4));
    // Bit 13 = OWN bit (0 = owned by host, 1 = owned by card)
    // We'll just proceed - if buffer is busy, we'll overwrite (simple implementation)
    
    // Copy data to transmit buffer
    for (uint16_t i = 0; i < length; i++) {
        g_rtl8139.tx_buffer[tx_desc][i] = data[i];
    }
    
    // Set transmit address (physical address of buffer)
    outl(io_base + RTL8139_TXADDR0 + (tx_desc * 4), g_rtl8139.tx_buffer_phys[tx_desc]);
    
    // Set transmit status (length triggers transmission)
    // Bit 13 = OWN (set to 1 to give buffer to card)
    // Lower 12 bits = packet length
    outl(io_base + RTL8139_TXSTATUS0 + (tx_desc * 4), length | (1 << 13));
    
    // Move to next TX descriptor
    g_rtl8139.current_tx = (g_rtl8139.current_tx + 1) % 4;
    
    return 0;
}

// Receive packet
int rtl8139_receive_packet(uint8_t* buffer, uint16_t max_length) {
    if (!g_rtl8139.initialized || buffer == NULL) {
        return -1;
    }
    
    uint16_t io_base = g_rtl8139.io_base;
    
    // Check if there are packets available (check ISR for ROK bit)
    uint16_t isr = inw(io_base + RTL8139_ISR);
    if (!(isr & RTL8139_ISR_ROK)) {
        return -1;  // No packets available
    }
    
    // Read current packet read pointer
    uint16_t current = inw(io_base + RTL8139_CAPR);
    
    // Calculate offset in receive buffer
    uint16_t offset = current % RTL8139_RX_BUF_SIZE;
    
    // Read packet header (first 4 bytes: status, length)
    // Note: status and length are in little-endian format
    uint16_t status = *(uint16_t*)(g_rtl8139.rx_buffer + offset);
    uint16_t length = *((uint16_t*)(g_rtl8139.rx_buffer + offset) + 1);
    
    // Check if packet is valid (status bit 0 = packet OK)
    if ((status & 0x01) == 0) {
        // No valid packet at this position, clear ROK and return
        outw(io_base + RTL8139_ISR, RTL8139_ISR_ROK);
        return -1;
    }
    
    // Extract length (lower 13 bits, in bytes, includes 4-byte header)
    length = length & 0x1FFF;
    
    // Check minimum packet size
    if (length < 4) {
        // Invalid packet, skip it
        current = (current + 4 + 3) & ~3;
        outw(io_base + RTL8139_CAPR, current - 0x10);
        outw(io_base + RTL8139_ISR, RTL8139_ISR_ROK);
        return -1;
    }
    
    // Remove header (4 bytes) to get actual data length
    uint16_t data_len = length - 4;
    
    // Check CRC (status bit 1 = CRC OK)
    if ((status & 0x02) == 0) {
        // CRC error, but we'll still process it for now
    }
    
    if (data_len > max_length) {
        data_len = max_length;
    }
    
    // Copy packet data (skip 4-byte header)
    for (uint16_t i = 0; i < data_len; i++) {
        buffer[i] = g_rtl8139.rx_buffer[offset + 4 + i];
    }
    
    // Update CAPR (Current Address of Packet Read)
    // Move to next packet (align to 4 bytes)
    current = (current + length + 3) & ~3;
    // Update CAPR (subtract 0x10 as per RTL8139 spec)
    outw(io_base + RTL8139_CAPR, current - 0x10);
    
    // Clear ROK interrupt
    outw(io_base + RTL8139_ISR, RTL8139_ISR_ROK);
    
    return data_len;
}

// Get MAC address
void rtl8139_get_mac(uint8_t* mac) {
    if (mac == NULL) return;
    for (int i = 0; i < 6; i++) {
        mac[i] = g_rtl8139.mac_address[i];
    }
}

// IRQ handler
void rtl8139_irq_handler(void) {
    if (!g_rtl8139.initialized) {
        return;
    }
    
    uint16_t io_base = g_rtl8139.io_base;
    uint16_t status = inw(io_base + RTL8139_ISR);
    
    // Acknowledge interrupts
    outw(io_base + RTL8139_ISR, status);
    
    // Handle receive interrupt
    if (status & RTL8139_ISR_ROK) {
        // Process received packets
        uint8_t frame[1514];
        int length;
        while ((length = ethernet_receive_frame(frame, sizeof(frame))) > 0) {
            ethernet_process_frame(frame, length);
        }
    }
    
    // Handle transmit interrupt
    if (status & RTL8139_ISR_TOK) {
        // Packet transmitted
    }
}


