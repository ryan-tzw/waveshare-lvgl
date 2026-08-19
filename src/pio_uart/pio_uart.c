#include "pio_uart.h"

#include "hardware/irq.h"
#include "pico/assert.h"
#include "uart_rx.pio.h"
#include "uart_tx.pio.h"

#define PIO_UART_MAX_INSTANCES 4
#ifndef PIO_UART_RX_QUEUE_SIZE
#define PIO_UART_RX_QUEUE_SIZE 256
#endif

static PioUart *registered_uarts[PIO_UART_MAX_INSTANCES];
static bool     pio_irq_configured[NUM_PIOS];
static uint     pio_irq_index_for_block[NUM_PIOS];

static void pio_uart_irq_handler(void) {
    for (size_t i = 0; i < PIO_UART_MAX_INSTANCES; ++i) {
        PioUart *uart = registered_uarts[i];

        if (uart == NULL || !uart->initialized) { continue; }

        while (!pio_sm_is_rx_fifo_empty(uart->rx_pio, uart->rx_sm)) {
            uint8_t byte = (uint8_t)uart_rx_program_getc(uart->rx_pio, uart->rx_sm);

            if (!queue_try_add(&uart->rx_queue, &byte)) {
                ++uart->rx_dropped_bytes;
            }
        }
    }
}

static int pio_uart_find_registry_slot(const PioUart *uart) {
    int empty_slot = -1;

    for (size_t i = 0; i < PIO_UART_MAX_INSTANCES; ++i) {
        if (registered_uarts[i] == uart) { return -1; }

        if (registered_uarts[i] == NULL && empty_slot < 0) {
            empty_slot = (int)i;
        }
    }

    return empty_slot;
}

static bool pio_uart_configure_irq(PIO pio, uint *irq_index, int *irq_num) {
    const uint pio_index = pio_get_index(pio);

    if (pio_irq_configured[pio_index]) {
        *irq_index = pio_irq_index_for_block[pio_index];
        *irq_num   = pio_get_irq_num(pio, *irq_index);
        return true;
    }

    for (uint candidate = 0; candidate < NUM_PIO_IRQS; ++candidate) {
        const int candidate_irq = pio_get_irq_num(pio, candidate);

        if (irq_get_exclusive_handler((uint)candidate_irq) != NULL) { continue; }

        irq_add_shared_handler(
            (uint)candidate_irq,
            pio_uart_irq_handler,
            PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY
        );
        irq_set_enabled((uint)candidate_irq, true);

        pio_irq_index_for_block[pio_index] = candidate;
        pio_irq_configured[pio_index]      = true;
        *irq_index = candidate;
        *irq_num   = candidate_irq;
        return true;
    }

    return false;
}

static void pio_uart_release_programs(PioUart *uart) {
    pio_sm_set_enabled(uart->rx_pio, uart->rx_sm, false);
    pio_remove_program_and_unclaim_sm(
        &uart_rx_program,
        uart->rx_pio,
        uart->rx_sm,
        uart->rx_offset
    );

    pio_sm_set_enabled(uart->tx_pio, uart->tx_sm, false);
    pio_remove_program_and_unclaim_sm(
        &uart_tx_program,
        uart->tx_pio,
        uart->tx_sm,
        uart->tx_offset
    );
}

bool pio_uart_init(PioUart *uart, uint tx_pin, uint rx_pin, uint baud_rate) {
    if (uart == NULL || uart->initialized || baud_rate == 0) { return false; }

    const int registry_slot = pio_uart_find_registry_slot(uart);
    if (registry_slot < 0) { return false; }

    /* =========================
        Attempt to claim SMs
       ========================= */

    const bool tx_program_claimed = pio_claim_free_sm_and_add_program_for_gpio_range(
        &uart_tx_program,
        &uart->tx_pio,
        &uart->tx_sm,
        &uart->tx_offset,
        tx_pin,
        1,
        true
    );
    if (!tx_program_claimed) { return false; }

    const bool rx_program_claimed = pio_claim_free_sm_and_add_program_for_gpio_range(
        &uart_rx_program,
        &uart->rx_pio,
        &uart->rx_sm,
        &uart->rx_offset,
        rx_pin,
        1,
        true
    );
    if (!rx_program_claimed) {
        pio_remove_program_and_unclaim_sm(&uart_tx_program, uart->tx_pio, uart->tx_sm, uart->tx_offset);
        return false;
    }

    /* =======================================================
        Once we have the SMs we can initialise the programs
       ======================================================= */

    uart_tx_program_init(uart->tx_pio, uart->tx_sm, uart->tx_offset, tx_pin, baud_rate);
    uart_rx_program_init(uart->rx_pio, uart->rx_sm, uart->rx_offset, rx_pin, baud_rate);

    uart->tx_pin           = tx_pin;
    uart->rx_pin           = rx_pin;
    uart->baud_rate        = baud_rate;
    uart->rx_dropped_bytes = 0;

    queue_init(&uart->rx_queue, sizeof(uint8_t), PIO_UART_RX_QUEUE_SIZE);
    if (uart->rx_queue.data == NULL) {
        pio_uart_release_programs(uart);
        return false;
    }

    if (!pio_uart_configure_irq(uart->rx_pio, &uart->rx_irq_index, &uart->rx_irq)) {
        queue_free(&uart->rx_queue);
        pio_uart_release_programs(uart);
        return false;
    }

    registered_uarts[registry_slot] = uart;
    uart->initialized = true;

    pio_set_irqn_source_enabled(
        uart->rx_pio,
        uart->rx_irq_index,
        pio_get_rx_fifo_not_empty_interrupt_source(uart->rx_sm),
        true
    );

    return true;
}

void pio_uart_write(PioUart *uart, const uint8_t *data, size_t length) {
    hard_assert(uart != NULL);
    hard_assert(uart->initialized);
    hard_assert(data != NULL || length == 0);

    for (size_t i = 0; i < length; ++i) {
        uart_tx_program_putc(uart->tx_pio, uart->tx_sm, (char)data[i]);
    }
}

bool pio_uart_readable(const PioUart *uart) {
    hard_assert(uart != NULL);
    hard_assert(uart->initialized);

    /*
     * Pico SDK 2.2.0's queue inspection API is not const-correct, although it
     * does not change the queue contents.
     */
    return !queue_is_empty((queue_t *)&uart->rx_queue);
}

bool pio_uart_try_read(PioUart *uart, uint8_t *byte) {
    hard_assert(uart != NULL);
    hard_assert(byte != NULL);
    hard_assert(uart->initialized);

    return queue_try_remove(&uart->rx_queue, byte);
}

uint32_t pio_uart_rx_dropped_bytes(const PioUart *uart) {
    hard_assert(uart != NULL);
    hard_assert(uart->initialized);

    return uart->rx_dropped_bytes;
}
