#ifndef PIO_H
#define PIO_H

#include <stdio.h>
#include <stdlib.h>

#include "hardware/dma.h"
#include "hardware/pio.h"
#include "pico/mutex.h"

// #define SERIAL_BAUD PICO_DEFAULT_UART_BAUD_RATE
// #define SERIAL_BAUD 110
// #define SERIAL_BAUD 300
// #define SERIAL_BAUD 1200
// #define SERIAL_BAUD 2400
// #define SERIAL_BAUD 4800
// #define SERIAL_BAUD 9600 // Common default for sensors and
// householdelectronics #define SERIAL_BAUD 14400
// #define SERIAL_BAUD \
//   19200  // Common for embedded systems BEST FOR OUR USE CASE!!! POTENTIALLY
//   CAN
//          // BE INCREASED IF WE IMPLEMENT DMA TO RX AND TX OR IMPROVE
//          // HARDWARE/BATTERY/POWER RAIL
// #define SERIAL_BAUD 38400 // Common for debugging and industrial controllers
// #define SERIAL_BAUD 57600 // High-speed serial devices
#define SERIAL_BAUD 115200 // Standard default for USB-to-UART bridges and
// modern computers #define SERIAL_BAUD 230400 #define SERIAL_BAUD 460800
// #define SERIAL_BAUD 921600
// #define SERIAL_BAUD 1000000
// #define SERIAL_BAUD 2000000
// #define SERIAL_BAUD 3000000 // common in FTDI chips
// #define SERIAL_BAUD 12000000 // maximum limit on high-speed FTDI hardware

#define DMA_BUFFER_CAPACITY 256

typedef struct UART_RX_CONFIG {
  uint pin;
  PIO pio;
  uint sm;
  uint offset;

  uint8_t dma_buffer_rx[DMA_BUFFER_CAPACITY]
      __attribute__((aligned(DMA_BUFFER_CAPACITY)));
  dma_channel_config dma_channel_rx_config;
  uint dma_buffer_rx_tail;
  uint dma_buffer_rx_head;
  uint dma_channel_rx;

} UART_RX_CONFIG;

void uart_rx_pio_on_gpio(UART_RX_CONFIG *uart_rx_config);

bool dma_chan_has_unprocessed_data(UART_RX_CONFIG *uart_rx_cfg);

typedef struct UART_TX_CONFIG {
  uint pin;
  PIO pio;
  uint sm;
  uint offset;
  mutex_t mutex;

  uint8_t dma_buffer_tx[DMA_BUFFER_CAPACITY]
      __attribute__((aligned(DMA_BUFFER_CAPACITY)));
  uint8_t dma_linear_buffer_tx[DMA_BUFFER_CAPACITY]
      __attribute__((aligned(DMA_BUFFER_CAPACITY)));
  dma_channel_config dma_channel_tx_config;
  volatile uint dma_buffer_tx_tail;
  volatile uint dma_buffer_tx_head;
  uint dma_channel_tx;
  uint dma_irq_index; // [0, 3]
  void (*dma_irq_handler_wrapper)(void);
  volatile uint kicked;
  volatile bool pending_kicked_transfer;
} UART_TX_CONFIG;

void uart_tx_pio_on_gpio(UART_TX_CONFIG *uart_tx_config);

bool queue_into_data_into_dma_buffer(UART_TX_CONFIG *uart_tx_config,
                                     uint8_t *data, size_t len);

void uart_tx_kick_dma(UART_TX_CONFIG *uart_tx_config);

void dma_irq_handler(UART_TX_CONFIG *uart_tx_config);

void put_uart_tx(UART_TX_CONFIG *uart_tx_config, const uint8_t *buf,
                 size_t len);

#endif