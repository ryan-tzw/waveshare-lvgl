#ifndef PIO_H
#define PIO_H

#include <stdio.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "pico/async_context_threadsafe_background.h"
#include "pico/sync.h"
#include "hardware/dma.h"

#include "hardware/pio.h"
#include "hardware/uart.h"

#include "uart_rx.pio.h"
#include "uart_tx.pio.h"

#include "protocol.pb.h"
#include "pb_encode.h"
#include "pb_decode.h"

#include "crc16.h"

// #define SERIAL_BAUD PICO_DEFAULT_UART_BAUD_RATE
// #define SERIAL_BAUD 110
// #define SERIAL_BAUD 300
// #define SERIAL_BAUD 1200
// #define SERIAL_BAUD 2400
// #define SERIAL_BAUD 4800
// #define SERIAL_BAUD 9600 // Common default for sensors and household electronics
// #define SERIAL_BAUD 14400 //
#define SERIAL_BAUD 19200 // Common for embedded systems BEST FOR OUR USE CASE!!! POTENTIALLY CAN BE INCREASED IF WE IMPLEMENT DMA TO RX AND TX OR IMPROVE HARDWARE/BATTERY/POWER RAIL
// #define SERIAL_BAUD 38400 // Common for debugging and industrial controllers
// #define SERIAL_BAUD 57600 // High-speed serial devices
// #define SERIAL_BAUD 115200 // Standard default for USB-to-UART bridges and modern computers
// #define SERIAL_BAUD 230400
// #define SERIAL_BAUD 460800
// #define SERIAL_BAUD 921600
// #define SERIAL_BAUD 1000000
// #define SERIAL_BAUD 2000000
// #define SERIAL_BAUD 3000000 // common in FTDI chips
// #define SERIAL_BAUD 12000000 // maximum limit on high-speed FTDI hardware
#define UART_FIFO_QUEUE_SIZE_BYTES 128

#define ETHERNET_HEADER_SIZE_BYTES 2
#define CRC_SIZE_BYTES 2
#define ETHERNET_FRAME_MIN_SIZE_BYTES (ETHERNET_HEADER_SIZE_BYTES + CRC_SIZE_BYTES)
#define ETHERNET_FRAME_MAX_SIZE_BYTES UART_FIFO_QUEUE_SIZE_BYTES
#define ETHERNET_PAYLOAD_MAX_SIZE_BYTES (ETHERNET_FRAME_MAX_SIZE_BYTES - ETHERNET_HEADER_SIZE_BYTES - CRC_SIZE_BYTES)
#define COBS_MAX_SIZE_BYTES (ETHERNET_FRAME_MAX_SIZE_BYTES + (ETHERNET_FRAME_MAX_SIZE_BYTES/254) + 2)
#define TCP_SEGMENT_TIMEOUT_TIMER 4
#define TCP_SOCKET_TIMEOUT_TIMER 128
#define TCP_SOCKET_PREVENT_TIMEOUT_TIMER 32

typedef struct UART_RX_CONFIG {
    uint pin;
    PIO pio;
    uint sm;
    uint offset;
    queue_t fifo;
    void (*pio_irq_func_wrapper)(void);
    async_when_pending_worker_t worker;
    async_context_threadsafe_background_t async_context;
    uint8_t cobs_buffer[COBS_MAX_SIZE_BYTES];
    int cobs_index;
} UART_RX_CONFIG;

void uart_rx_pio_irq_func(UART_RX_CONFIG* uart_rx_config);

void uart_rx_pio_on_gpio(UART_RX_CONFIG* uart_rx_config);

typedef struct UART_TX_CONFIG {
    uint pin;
    PIO pio;
    uint sm;
    uint offset;
    mutex_t mutex;
} UART_TX_CONFIG;

void uart_tx_pio_on_gpio(UART_TX_CONFIG* uart_tx_config);

#endif