#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pio_uart.h"

#define FRAMED_UART_MAX_PAYLOAD_SIZE 240
#define FRAMED_UART_CRC_SIZE 2
#define FRAMED_UART_MAX_DATA_SIZE (FRAMED_UART_MAX_PAYLOAD_SIZE + FRAMED_UART_CRC_SIZE)
#define FRAMED_UART_MAX_ENCODED_SIZE (FRAMED_UART_MAX_DATA_SIZE + 1)
#define FRAMED_UART_MAX_FRAME_SIZE (FRAMED_UART_MAX_ENCODED_SIZE + 1)

typedef struct {
    PioUart *uart;
    uint8_t tx_data_buffer[FRAMED_UART_MAX_DATA_SIZE];
    uint8_t tx_buffer[FRAMED_UART_MAX_FRAME_SIZE];
    uint8_t rx_buffer[FRAMED_UART_MAX_ENCODED_SIZE];
    uint8_t rx_data_buffer[FRAMED_UART_MAX_DATA_SIZE];
    size_t rx_length;
    bool discarding_frame;
    bool initialized;
} FramedUart;

/* A FramedUart must be zero-initialized before first use. */
bool framed_uart_init(FramedUart *framed_uart, PioUart *uart);

/*
 * Sends one complete frame using the underlying UART's blocking write.
 * A NULL payload is valid only when length is zero.
 */
bool framed_uart_send(
    FramedUart *framed_uart,
    const uint8_t *payload,
    size_t length
);

/*
 * Drains currently queued UART bytes until one valid frame is copied or no
 * more bytes remain. Invalid frames are silently discarded. The output length
 * is set to zero unless a complete frame is returned.
 */
bool framed_uart_try_receive(
    FramedUart *framed_uart,
    uint8_t *payload,
    size_t capacity,
    size_t *length
);
