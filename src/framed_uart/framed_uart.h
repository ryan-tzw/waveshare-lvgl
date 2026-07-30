#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pio_uart.h"

#define FRAMED_UART_MAX_PAYLOAD_SIZE 240
#define FRAMED_UART_MAX_ENCODED_SIZE 241
#define FRAMED_UART_MAX_FRAME_SIZE 242

typedef struct {
    PioUart *uart;
    uint8_t tx_buffer[FRAMED_UART_MAX_FRAME_SIZE];
    uint8_t rx_buffer[FRAMED_UART_MAX_ENCODED_SIZE];
    size_t rx_length;
    bool discarding_frame;
    bool initialized;
} FramedUart;

/* A FramedUart must be zero-initialized before first use. */
bool framed_uart_init(FramedUart *framed_uart, PioUart *uart);
bool framed_uart_send(
    FramedUart *framed_uart,
    const uint8_t *payload,
    size_t length
);
bool framed_uart_try_receive(
    FramedUart *framed_uart,
    uint8_t *payload,
    size_t capacity,
    size_t *length
);
