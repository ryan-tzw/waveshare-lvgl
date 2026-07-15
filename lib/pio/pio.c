#include "pio.h"

#define DMA_IRQ_PRIORITY PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY

// TODO: We should be able to attach more than 2 callbacks on every PIO despite having only 2 IRQ lines. Right now we are panicking if we cant attach more than 2 callbacks as exclusive handlers to a PIO. 

static int8_t get_other_pio_irq(int8_t pio_irq) { 
    /**
     * Only 15-20 IRQ and 2 IRQ for every PIO even though 4 SM
     * Section 15.1 of https://pico.implrust.com/interrupts/interrupts-in-rp2350.html
     */
    if ((pio_irq < 15) || (pio_irq > 20)) {
        panic("Unknown PIO IRQ %d", pio_irq);
    }
    if (pio_irq % 2 == 0) {
        return pio_irq - 1;
    } else {
        return pio_irq + 1;
    }
}

void uart_rx_pio_irq_func(UART_RX_CONFIG* uart_rx_config) {
    while(!pio_sm_is_rx_fifo_empty(uart_rx_config->pio, uart_rx_config->sm)) {
        char c = uart_rx_program_getc(uart_rx_config->pio, uart_rx_config->sm);
        if (!queue_try_add(&uart_rx_config->fifo, &c)) {
            panic("fifo full");
        }
    }
    // Tell the async worker that there are some characters waiting for us
    async_context_set_work_pending(&uart_rx_config->async_context.core, &uart_rx_config->worker);
}

void uart_rx_pio_on_gpio(UART_RX_CONFIG* uart_rx_config) {
    if (uart_rx_config->pin >= NUM_BANK0_GPIOS)
        panic("Attempting to use a pin>=32 on a platform that does not support it"); 

    // create a queue so the irq can save the data somewhere
    queue_init(&uart_rx_config->fifo, 1, UART_FIFO_QUEUE_SIZE_BYTES); // Queue has 128 members capacity with each member at 1 byte

    // Setup an async context and worker to perform work when needed
    if (!async_context_threadsafe_background_init_with_defaults(&uart_rx_config->async_context)) {
        panic("failed to setup context");
    }
    async_context_add_when_pending_worker(&uart_rx_config->async_context.core, &uart_rx_config->worker);

    // Find a PIO and load the UART RX program into it, then assign the GPIO pin to it 
    bool success = pio_claim_free_sm_and_add_program_for_gpio_range(&uart_rx_program, &uart_rx_config->pio, &uart_rx_config->sm, &uart_rx_config->offset, uart_rx_config->pin, 1, true);
    hard_assert(success);
    uart_rx_program_init(uart_rx_config->pio, uart_rx_config->sm, uart_rx_config->offset, uart_rx_config->pin, SERIAL_BAUD);

    // Find a free irq
    // int8_t pio_irq = pio_get_irq_num(uart_rx_config->pio, 0);
    // irq_handler_t irq_handler = irq_get_exclusive_handler(pio_irq);
    // if (irq_handler == NULL) {
    //     // Enable interrupt
    //     irq_set_exclusive_handler(pio_irq, pio_exclusive_handler);
    //     // irq_add_shared_handler(pio_irq, uart_rx_config->pio_irq_func_wrapper, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY); // Add a shared IRQ handler
    //     irq_set_enabled(pio_irq, true); // Enable the IRQ
    //     const uint irq_index = pio_irq - pio_get_irq_num(uart_rx_config->pio, 0); // Get index of the IRQ
    //     pio_set_irqn_source_enabled(uart_rx_config->pio, irq_index, pio_get_rx_fifo_not_empty_interrupt_source(uart_rx_config->sm), true); // Set pio to tell us when the FIFO is NOT empty
    // } 
    // if (irq_handler != NULL && irq_handler != pio_exclusive_handler) {
    //     panic("IRQ %d for SM %d PIO %d is in use by another exclusive handler", pio_irq, uart_rx_config->sm, uart_rx_config->pio);
    // }
    // if (uart_rx_programs >= MAX_UART_RX_PROGRAMS) {
    //     panic("Too many UART RX IRQ handlers");
    // }
    // uart_rx_pio_irq_func_wrappers[uart_rx_programs++] = uart_rx_config->pio_irq_func_wrapper;

    // Find a free irq
    int8_t pio_irq = pio_get_irq_num(uart_rx_config->pio, 0);
    if (irq_get_exclusive_handler(pio_irq)) {
        pio_irq = get_other_pio_irq(pio_irq);
        if (irq_get_exclusive_handler(pio_irq)) {
            panic("uart_rx_pio_on_gpio: All IRQ in use");
        }
    }
    // Enable interrupt
    irq_set_exclusive_handler(pio_irq, uart_rx_config->pio_irq_func_wrapper);
    irq_set_enabled(pio_irq, true); // Enable the IRQ
    uint irq_index;
    if (pio_irq > get_other_pio_irq(pio_irq)) {
        irq_index = 1;
    } else {
        irq_index = 0;
    }
    pio_set_irqn_source_enabled(uart_rx_config->pio, irq_index, pio_get_rx_fifo_not_empty_interrupt_source(uart_rx_config->sm), true); // Set pio to tell us when the FIFO is NOT empty
    printf("uart_rx_pio_on_gpio: Installed UART RX program on %u, %d, %u, %d (sm, pio, pin, IRQ)\n", uart_rx_config->sm, uart_rx_config->pio, uart_rx_config->pin, pio_irq);

    char buffer_tx[] = "the quick brown fox jumps over the lazy dog";
    char buffer_rx[sizeof(buffer_tx) - 1] = {0};
    
    // uint dma_channel_rx = dma_claim_unused_channel(true);
    // dma_channel_config config_rx = dma_channel_get_default_config(dma_channel_rx);
    // channel_config_set_transfer_data_size(&config_rx, DMA_SIZE_8);
    // channel_config_set_read_increment(&config_rx, false);
    // channel_config_set_write_increment(&config_rx, true);
    // uint32_t read_size = sizeof(buffer_tx) - 1;
    // // enable irq for rx
    // dma_irqn_set_channel_enabled(0, dma_channel_rx, true); // DMA IRQ to use from 0 to 3
    // // setup dma to read from pio fifo
    // channel_config_set_dreq(&config_rx, pio_get_dreq(pio_hw_rx, pio_sm_rx, false));
    // // 8-bit read from the uppermost byte of the FIFO, as data is left-justified so need to add 3. Don't forget the cast!
    // dma_channel_configure(dma_channel_rx, &config_rx, buffer_rx, (io_rw_8*)&pio_hw_rx->rxf[pio_sm_rx] + 3, read_size, true); // dma started
    // uint dma_channel_tx = 0;
    // printf("nothing fucked up\n");
}

void uart_tx_pio_on_gpio(UART_TX_CONFIG* uart_tx_config) {
    mutex_init(&uart_tx_config->mutex);
    // Find a PIO and load the UART TX program into it, then assign the GPIO pin to it 
    bool success = pio_claim_free_sm_and_add_program_for_gpio_range(&uart_tx_program, &uart_tx_config->pio, &uart_tx_config->sm, &uart_tx_config->offset, uart_tx_config->pin, 1, true);
    hard_assert(success);
    uart_tx_program_init(uart_tx_config->pio, uart_tx_config->sm, uart_tx_config->offset, uart_tx_config->pin, SERIAL_BAUD);

    printf("uart_tx_pio_on_gpio: Installed UART TX program on %u, %d, %u (sm, pio, pin)\n", uart_tx_config->sm, uart_tx_config->pio, uart_tx_config->pin);
}
