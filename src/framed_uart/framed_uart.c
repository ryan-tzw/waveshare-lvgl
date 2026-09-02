#include "framed_uart.h"

#include <string.h>

#include "checksum.h"
#include "cobs.h"
#include "pico/assert.h"

bool framed_uart_init(FramedUart *framed_uart, PioUart *uart) {
    if (framed_uart == NULL || uart == NULL) { return false; }
    if (framed_uart->initialized || !uart->initialized) { return false; }

    framed_uart->uart              = uart;
    framed_uart->rx_encoded_length = 0;
    framed_uart->received_frames   = 0;
    framed_uart->cobs_errors       = 0;
    framed_uart->crc_errors        = 0;
    framed_uart->oversized_frames  = 0;
    framed_uart->discarding_frame  = false;
    framed_uart->initialized       = true;

    return true;
}

bool framed_uart_send(FramedUart *framed_uart, const uint8_t *payload, size_t length) {
    if (framed_uart == NULL || !framed_uart->initialized) { return false; }
    if ((payload == NULL && length != 0) || length > FRAMED_UART_MAX_PAYLOAD_SIZE) { return false; }

    if (length > 0) {
        memcpy(framed_uart->tx_unencoded_buffer, payload, length);
    }

    uint16_t crc = crc_modbus(framed_uart->tx_unencoded_buffer, length);
    /* CRC-16/MODBUS is sent least-significant byte first. */
    framed_uart->tx_unencoded_buffer[length]     = (uint8_t)(crc & 0xff);
    framed_uart->tx_unencoded_buffer[length + 1] = (uint8_t)(crc >> 8);

    cobs_encode_result encode_result = cobs_encode(
        framed_uart->tx_encoded_buffer,
        FRAMED_UART_MAX_ENCODED_SIZE,
        framed_uart->tx_unencoded_buffer,
        length + FRAMED_UART_CRC_SIZE
    );

    if (encode_result.status != COBS_ENCODE_OK) { return false; }

    framed_uart->tx_encoded_buffer[encode_result.out_len] = 0;
    pio_uart_write(framed_uart->uart, framed_uart->tx_encoded_buffer, encode_result.out_len + 1);

    return true;
}

bool framed_uart_try_receive(
    FramedUart *framed_uart,
    uint8_t    *payload,
    size_t      capacity,
    size_t     *length
) {
    if (length != NULL) { *length = 0; }

    if (framed_uart == NULL       ||
        !framed_uart->initialized ||
        payload == NULL           ||
        capacity == 0             ||
        length == NULL
    ) { return false; }

    uint8_t byte;
    while (pio_uart_try_read(framed_uart->uart, &byte)) {
        if (byte != 0) {
            if (framed_uart->discarding_frame) { continue; }

            if (framed_uart->rx_encoded_length >= FRAMED_UART_MAX_ENCODED_SIZE) {
                framed_uart->rx_encoded_length = 0;
                framed_uart->discarding_frame  = true;
                framed_uart->oversized_frames++;
                continue;
            }

            framed_uart->rx_encoded_buffer[framed_uart->rx_encoded_length] = byte;
            framed_uart->rx_encoded_length++;
            continue;
        }

        if (framed_uart->discarding_frame) {
            framed_uart->discarding_frame  = false;
            framed_uart->rx_encoded_length = 0;
            continue;
        }

        if (framed_uart->rx_encoded_length == 0) { continue; }

        cobs_decode_result decode_result = cobs_decode(
            framed_uart->rx_decoded_buffer,
            FRAMED_UART_MAX_DATA_SIZE,
            framed_uart->rx_encoded_buffer,
            framed_uart->rx_encoded_length
        );

        framed_uart->rx_encoded_length = 0;

        if (decode_result.status != COBS_DECODE_OK) {
            framed_uart->cobs_errors++;
            continue;
        }

        if (decode_result.out_len < FRAMED_UART_CRC_SIZE) {
            framed_uart->crc_errors++;
            continue;
        }

        size_t   payload_length = decode_result.out_len - FRAMED_UART_CRC_SIZE;
        uint8_t  low_byte       = framed_uart->rx_decoded_buffer[payload_length];
        uint8_t  high_byte      = framed_uart->rx_decoded_buffer[payload_length + 1];
        uint16_t received_crc   = ((uint16_t)high_byte << 8) | low_byte;
        uint16_t expected_crc   = crc_modbus(framed_uart->rx_decoded_buffer, payload_length);

        if (received_crc != expected_crc) {
            framed_uart->crc_errors++;
            continue;
        }

        if (payload_length > capacity) {
            framed_uart->oversized_frames++;
            continue;
        }

        if (payload_length > 0) {
            memcpy(payload, framed_uart->rx_decoded_buffer, payload_length);
        }

        *length = payload_length;
        framed_uart->received_frames++;
        return true;
    }

    return false;
}

FramedUartStatistics framed_uart_get_statistics(const FramedUart *framed_uart) {
    hard_assert(framed_uart != NULL);
    hard_assert(framed_uart->initialized);

    FramedUartStatistics statistics = {
        .received_frames    = framed_uart->received_frames,
        .cobs_errors        = framed_uart->cobs_errors,
        .crc_errors         = framed_uart->crc_errors,
        .oversized_frames   = framed_uart->oversized_frames,
        .uart_dropped_bytes = pio_uart_rx_dropped_bytes(framed_uart->uart)
    };

    return statistics;
}
