#include "pio.h"

#include "logging.h"
#include "uart_rx.pio.h"
#include "uart_tx.pio.h"

#define DMA_IRQ_PRIORITY PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY

void uart_rx_pio_on_gpio(UART_RX_CONFIG *uart_rx_config) {
  if (uart_rx_config->pin >= NUM_BANK0_GPIOS)
    panic("Attempting to use a pin>=32 on a platform that does not support it");

  // Load the UART RX program into PIO, then assign the GPIO pin
  if (pio_sm_is_claimed(uart_rx_config->pio, uart_rx_config->sm)) {
    panic("uart_rx_pio_on_gpio: PIO %d sm %u already claimed",
          uart_rx_config->pio, uart_rx_config->sm);
  }
  pio_sm_claim(uart_rx_config->pio, uart_rx_config->sm);
  uart_rx_config->offset =
      pio_add_program(uart_rx_config->pio, &uart_rx_program);
  if (uart_rx_config->pin >= 16) {
    pio_set_gpio_base(uart_rx_config->pio, 16);
  } else {
    pio_set_gpio_base(uart_rx_config->pio, 0);
  }
  uart_rx_program_init(uart_rx_config->pio, uart_rx_config->sm,
                       uart_rx_config->offset, uart_rx_config->pin,
                       SERIAL_BAUD);

  // Use DMA
  uart_rx_config->dma_channel_rx = dma_claim_unused_channel(true);
  uart_rx_config->dma_channel_rx_config =
      dma_channel_get_default_config(uart_rx_config->dma_channel_rx);
  channel_config_set_transfer_data_size(&uart_rx_config->dma_channel_rx_config,
                                        DMA_SIZE_8);
  channel_config_set_read_increment(&uart_rx_config->dma_channel_rx_config,
                                    false);
  channel_config_set_write_increment(&uart_rx_config->dma_channel_rx_config,
                                     true);
  channel_config_set_dreq(
      &uart_rx_config->dma_channel_rx_config,
      pio_get_dreq(uart_rx_config->pio, uart_rx_config->sm, false));
  channel_config_set_ring(&uart_rx_config->dma_channel_rx_config, true,
                          8); // true  = wrap write address;
  dma_channel_configure(
      uart_rx_config->dma_channel_rx, &uart_rx_config->dma_channel_rx_config,
      uart_rx_config->dma_buffer_rx,
      (io_rw_8 *)&uart_rx_config->pio->rxf[uart_rx_config->sm] + 3,
      dma_encode_endless_transfer_count(), true);

  LOG_INFO("uart_rx_pio_on_gpio: Installed UART RX program on %u, %d, %u (sm, "
           "pio, pin)",
           uart_rx_config->sm, uart_rx_config->pio, uart_rx_config->pin);
}

bool dma_chan_has_unprocessed_data(UART_RX_CONFIG *uart_rx_cfg) {
  dma_channel_hw_t *dma_chan = dma_channel_hw_addr(uart_rx_cfg->dma_channel_rx);

  uint32_t head =
      (dma_chan->write_addr - (uintptr_t)uart_rx_cfg->dma_buffer_rx) &
      (DMA_BUFFER_CAPACITY - 1);

  return head != uart_rx_cfg->dma_buffer_rx_head;
}

void uart_tx_pio_on_gpio(UART_TX_CONFIG *uart_tx_config) {
  mutex_init(&uart_tx_config->mutex);

  // Load the UART TX program into PIO, then assign the GPIO pin
  if (pio_sm_is_claimed(uart_tx_config->pio, uart_tx_config->sm)) {
    panic("uart_rx_pio_on_gpio: PIO %d sm %u already claimed",
          uart_tx_config->pio, uart_tx_config->sm);
  }
  pio_sm_claim(uart_tx_config->pio, uart_tx_config->sm);
  uart_tx_config->offset =
      pio_add_program(uart_tx_config->pio, &uart_tx_program);
  if (uart_tx_config->pin >= 16) {
    pio_set_gpio_base(uart_tx_config->pio, 16);
  } else {
    pio_set_gpio_base(uart_tx_config->pio, 0);
  }
  uart_tx_program_init(uart_tx_config->pio, uart_tx_config->sm,
                       uart_tx_config->offset, uart_tx_config->pin,
                       SERIAL_BAUD);
  pio_sm_set_enabled(uart_tx_config->pio, uart_tx_config->sm, true);
  // uart_tx_config->dma_channel_tx = dma_claim_unused_channel(true);
  // uart_tx_config->dma_channel_tx_config =
  //     dma_channel_get_default_config(uart_tx_config->dma_channel_tx);
  // channel_config_set_transfer_data_size(&uart_tx_config->dma_channel_tx_config,
  //                                       DMA_SIZE_8);
  // channel_config_set_read_increment(&uart_tx_config->dma_channel_tx_config,
  //                                   true);
  // channel_config_set_write_increment(&uart_tx_config->dma_channel_tx_config,
  //                                    false);
  // channel_config_set_dreq(
  //     &uart_tx_config->dma_channel_tx_config,
  //     pio_get_dreq(uart_tx_config->pio, uart_tx_config->sm, true));
  // irq_set_exclusive_handler(dma_get_irq_num(uart_tx_config->dma_irq_index),
  //                           uart_tx_config->dma_irq_handler_wrapper);
  // irq_set_enabled(dma_get_irq_num(uart_tx_config->dma_irq_index), true);
  // dma_irqn_set_channel_enabled(uart_tx_config->dma_irq_index,
  //                              uart_tx_config->dma_channel_tx, true);
  LOG_INFO(
      "uart_tx_pio_on_gpio: Installed UART TX program on %u, %d, %u (sm, pio, "
      "pin)",
      uart_tx_config->sm, uart_tx_config->pio, uart_tx_config->pin);
}

bool queue_into_data_into_dma_buffer(UART_TX_CONFIG *uart_tx_config,
                                     uint8_t *data, size_t len) {
  uint used;
  if (uart_tx_config->dma_buffer_tx_head > uart_tx_config->dma_buffer_tx_tail)
    used =
        uart_tx_config->dma_buffer_tx_head - uart_tx_config->dma_buffer_tx_tail;
  else
    used = (DMA_BUFFER_CAPACITY - uart_tx_config->dma_buffer_tx_tail) +
           uart_tx_config->dma_buffer_tx_head;
  if (DMA_BUFFER_CAPACITY - used - 1 < len) {
    LOG_INFO("queue_into_data_into_dma_buffer: Not enough space %u, %u (tail, "
             "head)",
             uart_tx_config->dma_buffer_tx_tail,
             uart_tx_config->dma_buffer_tx_head);
    return false;
  }

  for (int i = 0; i < len; i++) {
    uart_tx_config->dma_buffer_tx[uart_tx_config->dma_buffer_tx_head] = *data++;
    uart_tx_config->dma_buffer_tx_head =
        (uart_tx_config->dma_buffer_tx_head + 1) % DMA_BUFFER_CAPACITY;
  }

  LOG_INFO("queue_into_data_into_dma_buffer: %u, %u (tail ,head)",
           uart_tx_config->dma_buffer_tx_tail,
           uart_tx_config->dma_buffer_tx_head);

  uart_tx_kick_dma(uart_tx_config);

  return true;
}

void uart_tx_kick_dma(UART_TX_CONFIG *uart_tx_config) {
  if (dma_channel_is_busy(uart_tx_config->dma_channel_tx)) {
    // LOG_INFO("uart_tx_kick_dma: DMA busy");
    return;
  }

  if (uart_tx_config->dma_buffer_tx_tail ==
      uart_tx_config->dma_buffer_tx_head) {
    // LOG_INFO("uart_tx_kick_dma: Nothing to send");
    return; // nothing to send
  }

  if (uart_tx_config->dma_buffer_tx_head > uart_tx_config->dma_buffer_tx_tail) {
    while (uart_tx_config->pending_kicked_transfer) {
      tight_loop_contents();
    };
    uart_tx_config->kicked =
        uart_tx_config->dma_buffer_tx_head - uart_tx_config->dma_buffer_tx_tail;
    uart_tx_config->pending_kicked_transfer = true;
    dma_channel_configure(
        uart_tx_config->dma_channel_tx, &uart_tx_config->dma_channel_tx_config,
        &uart_tx_config->pio->txf[uart_tx_config->sm], // destination
        &uart_tx_config
             ->dma_buffer_tx[uart_tx_config->dma_buffer_tx_tail], // source
        dma_encode_transfer_count(uart_tx_config->kicked), true);
    LOG_INFO("uart_tx_kick_dma: Wrap %u, %u (tail, head)",
             uart_tx_config->dma_buffer_tx_tail,
             uart_tx_config->dma_buffer_tx_head);
  } else {
    uint tail = uart_tx_config->dma_buffer_tx_tail;
    uint i = 0;
    while (tail != uart_tx_config->dma_buffer_tx_head) {
      uart_tx_config->dma_linear_buffer_tx[i++] =
          uart_tx_config->dma_buffer_tx[tail];
      tail = (tail + 1) % DMA_BUFFER_CAPACITY;
    }
    while (uart_tx_config->pending_kicked_transfer) {
      tight_loop_contents();
    };
    uart_tx_config->kicked = i;
    uart_tx_config->pending_kicked_transfer = true;
    dma_channel_configure(
        uart_tx_config->dma_channel_tx, &uart_tx_config->dma_channel_tx_config,
        &uart_tx_config->pio->txf[uart_tx_config->sm], // destination
        &uart_tx_config->dma_linear_buffer_tx,         // source
        dma_encode_transfer_count(uart_tx_config->kicked), true);
    LOG_INFO("uart_tx_kick_dma: Linear %u, %u (tail, head)",
             uart_tx_config->dma_buffer_tx_tail,
             uart_tx_config->dma_buffer_tx_head);
  }
}

void dma_irq_handler(UART_TX_CONFIG *uart_tx_config) {
  if (dma_irqn_get_channel_status(uart_tx_config->dma_irq_index,
                                  uart_tx_config->dma_channel_tx)) {
    dma_irqn_acknowledge_channel(uart_tx_config->dma_irq_index,
                                 uart_tx_config->dma_channel_tx);
    uart_tx_config->dma_buffer_tx_tail =
        (uart_tx_config->dma_buffer_tx_tail + uart_tx_config->kicked) %
        DMA_BUFFER_CAPACITY;
    uart_tx_config->pending_kicked_transfer = false;
  }
}

void put_uart_tx(UART_TX_CONFIG *uart_tx_config, const uint8_t *buf,
                 size_t len) {
  uart_tx_program_putuint8_buf(uart_tx_config->pio, uart_tx_config->sm, buf,
                               len);
};