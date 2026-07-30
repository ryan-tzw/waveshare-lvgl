#include "framed_uart.h"

#include "cobs.h"

bool framed_uart_init(FramedUart *framed_uart, PioUart *uart) {
    if (framed_uart == NULL || uart == NULL) {
        return false;
    }

    if (framed_uart->initialized || !uart->initialized) {
        return false;
    }

    framed_uart->uart = uart;
    framed_uart->rx_length = 0;
    framed_uart->discarding_frame = false;
    framed_uart->initialized = true;

    return true;
}

bool framed_uart_send(FramedUart *framed_uart, const uint8_t *payload, size_t length) {
    if (framed_uart == NULL || !framed_uart->initialized) {
        return false;
    }

    if ((payload == NULL && length != 0) || length > FRAMED_UART_MAX_PAYLOAD_SIZE) {
        return false;
    }

    uint8_t empty_payload = 0;
    const uint8_t *payload_to_encode = payload;

    if (length == 0) {
        payload_to_encode = &empty_payload;
    }

    cobs_encode_result result = cobs_encode(
        framed_uart->tx_buffer,
        FRAMED_UART_MAX_ENCODED_SIZE,
        payload_to_encode,
        length
    );

    if (result.status != COBS_ENCODE_OK) {
        return false;
    }

    framed_uart->tx_buffer[result.out_len] = 0;
    pio_uart_write(framed_uart->uart, framed_uart->tx_buffer, result.out_len + 1);

    return true;
}

bool framed_uart_try_receive(
    FramedUart *framed_uart,
    uint8_t *payload,
    size_t capacity,
    size_t *length
) {
    if (length != NULL) {
        *length = 0;
    }

    if (
        framed_uart == NULL ||
        !framed_uart->initialized ||
        payload == NULL ||
        capacity == 0 ||
        length == NULL
    ) {
        return false;
    }

    uint8_t byte;

    while (pio_uart_try_read(framed_uart->uart, &byte)) {
        if (byte != 0) {
            if (framed_uart->discarding_frame) {
                continue;
            }

            if (framed_uart->rx_length >= FRAMED_UART_MAX_ENCODED_SIZE) {
                framed_uart->rx_length = 0;
                framed_uart->discarding_frame = true;
                continue;
            }

            framed_uart->rx_buffer[framed_uart->rx_length] = byte;
            framed_uart->rx_length++;
            continue;
        }

        if (framed_uart->discarding_frame) {
            framed_uart->discarding_frame = false;
            framed_uart->rx_length = 0;
            continue;
        }

        if (framed_uart->rx_length == 0) {
            continue;
        }

        cobs_decode_result result = cobs_decode(
            payload,
            capacity,
            framed_uart->rx_buffer,
            framed_uart->rx_length
        );

        framed_uart->rx_length = 0;

        if (result.status == COBS_DECODE_OK) {
            *length = result.out_len;
            return true;
        }
    }

    return false;
}
