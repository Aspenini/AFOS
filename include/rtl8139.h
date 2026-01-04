#ifndef RTL8139_H
#define RTL8139_H

#include "types.h"

// RTL8139 PCI vendor/device IDs
#define RTL8139_VENDOR_ID 0x10EC
#define RTL8139_DEVICE_ID 0x8139

// RTL8139 I/O ports (base + offset)
#define RTL8139_PORT_COUNT 0x100

// RTL8139 registers
#define RTL8139_IDR0      0x00  // MAC address (6 bytes)
#define RTL8139_IDR4      0x04
#define RTL8139_IDR5      0x05
#define RTL8139_MAR0      0x08  // Multicast address (8 bytes)
#define RTL8139_TXSTATUS0 0x10  // Transmit status (4 TX descriptors)
#define RTL8139_TXADDR0   0x20  // Transmit address (4 TX descriptors)
#define RTL8139_RXBUF     0x30  // Receive buffer start address
#define RTL8139_CR        0x37  // Command register
#define RTL8139_CAPR      0x38  // Current address of packet read
#define RTL8139_IMR       0x3C  // Interrupt mask register
#define RTL8139_ISR       0x3E  // Interrupt status register
#define RTL8139_TCR       0x40  // Transmit configuration register
#define RTL8139_RCR       0x44  // Receive configuration register
#define RTL8139_CONFIG1   0x52  // Configuration register 1
#define RTL8139_MPC       0x4C  // Missed packet counter
#define RTL8139_9346CR    0x50  // EEPROM/93C46 command register

// Command register bits
#define RTL8139_CR_RST    0x10  // Reset
#define RTL8139_CR_RE     0x08  // Receiver enable
#define RTL8139_CR_TE     0x04  // Transmitter enable
#define RTL8139_CR_BUFE  0x01  // Buffer empty

// Interrupt status/mask bits
#define RTL8139_ISR_ROK   0x01  // Receive OK
#define RTL8139_ISR_RER   0x02  // Receive error
#define RTL8139_ISR_TOK   0x04  // Transmit OK
#define RTL8139_ISR_TER   0x08  // Transmit error
#define RTL8139_ISR_RXOVW 0x10 // Receive buffer overflow
#define RTL8139_ISR_PUN   0x20  // Packet underrun
#define RTL8139_ISR_FOVW  0x40  // FIFO overflow
#define RTL8139_ISR_CDF   0x80  // Cable disconnect

// Receive configuration register bits
#define RTL8139_RCR_AAP   0x01  // Accept all packets
#define RTL8139_RCR_APM   0x02  // Accept physical match
#define RTL8139_RCR_AM    0x04  // Accept multicast
#define RTL8139_RCR_AB    0x08  // Accept broadcast
#define RTL8139_RCR_WRAP  0x80  // Wrap mode

// Buffer sizes
#define RTL8139_RX_BUF_SIZE (8192 + 16 + 1500)  // 8KB + header + max packet
#define RTL8139_TX_BUF_SIZE 1536  // Max Ethernet frame size

// RTL8139 driver structure
typedef struct {
    uint16_t io_base;           // I/O base address
    uint8_t mac_address[6];     // MAC address
    uint8_t* rx_buffer;          // Receive buffer
    uint8_t* tx_buffer[4];      // Transmit buffers (4 descriptors)
    uint32_t rx_buffer_phys;     // Physical address of RX buffer
    uint32_t tx_buffer_phys[4]; // Physical addresses of TX buffers
    uint16_t current_tx;         // Current TX descriptor
    int initialized;
} rtl8139_t;

// Function prototypes
int rtl8139_init(void);
int rtl8139_send_packet(uint8_t* data, uint16_t length);
int rtl8139_receive_packet(uint8_t* buffer, uint16_t max_length);
void rtl8139_get_mac(uint8_t* mac);
void rtl8139_irq_handler(void);

// External reference to driver instance
extern rtl8139_t g_rtl8139;

#endif // RTL8139_H


