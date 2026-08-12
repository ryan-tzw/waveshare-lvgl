#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hardware/pio.h"
#include "pico/types.h"
#include "pico/util/queue.h"

typedef struct {
    PIO tx_pio;
    uint tx_sm;
    uint tx_offset;

    PIO rx_pio;
    uint rx_sm;
    uint rx_offset;

    uint tx_pin;
    uint rx_pin;
    uint baud_rate;

    queue_t rx_queue;

    int rx_irq;
    uint rx_irq_index;

    volatile uint32_t rx_dropped_bytes;

    bool initialized;
} PioUart;

/*
 * A PioUart must be zero-initialized before first use and must not be copied
 * after initialization.
 */
bool pio_uart_init(PioUart *uart, uint tx_pin, uint rx_pin, uint baud_rate);

/* TX uses the PIO FIFO's blocking write operation. */
void pio_uart_write_byte(PioUart *uart, uint8_t byte);
void pio_uart_write(PioUart *uart, const uint8_t *data, size_t length);

/* RX reads only from the interrupt-fed software queue and never blocks. */
bool pio_uart_readable(const PioUart *uart);
bool pio_uart_try_read(PioUart *uart, uint8_t *byte);

/* Returns the cumulative number of RX bytes dropped since initialization. */
uint32_t pio_uart_rx_dropped_bytes(const PioUart *uart);
