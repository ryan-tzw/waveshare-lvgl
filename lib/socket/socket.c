#include "socket.h"

#include "cobs.h"
#include "core1_task_queue.h"
#include "crc16.h"
#include "logging.h"
#include "pb_decode.h"
#include "pb_encode.h"

#define MAX_TCP_SOCKETS 4
static uint sockets_initialized = 0;
static TCP_SOCKET tcp_sockets[MAX_TCP_SOCKETS];

static void log_info_tcp_segment(const device_protocol_TCPSegment *segment) {
  if (segment == NULL) {
    LOG_WARN("TCPSegment: NULL");
    return;
  }

  LOG_INFO("=== TCPSegment ==="
           "sequence: %u"
           "ack: %s"
           "ack_sequence: %u"
           "to: %llu"
           "from: %llu"
           "syn: %s"
           "syn_ack: %s"
           "==================",
           segment->sequence, segment->ack ? "true" : "false",
           segment->ack_sequence, (unsigned long long)segment->to,
           (unsigned long long)segment->from, segment->syn ? "true" : "false",
           segment->syn_ack ? "true" : "false");
}

bool is_after_left_and_before_or_eq_right_sliding_window(uint left,
                                                         uint position,
                                                         uint right) {
  if (left <= right) {
    return left < position && position <= right;
  } else {
    return !(right < position && position <= left);
  }
}

bool is_at_least_left_and_before_right_sliding_window(uint left, uint position,
                                                      uint right) {
  if (left <= right) {
    return left <= position && position < right;
  } else {
    return !(right <= position && position < left);
  }
}

int sliding_window_length(uint left, uint right, uint capacity) {
  if (left <= right) {
    return right - left;
  } else {
    return (capacity - left) + right;
  }
}

// Unsafe due to static buffers
bool try_rcv_tcp_from_dma_buffer(UART_RX_CONFIG *uart_rx_cfg,
                                 device_protocol_TCPSegment *tcp_segment) {
  // dma_channel_hw_t* dma_chan =
  //     dma_channel_hw_addr(tcp_sockets[0].uart_rx_cfg.dma_channel_rx);

  // Increase head until delimiter or end of DMA transfer
  uint pin = uart_rx_cfg->pin;
  while (dma_chan_has_unprocessed_data(uart_rx_cfg)) {
    uint8_t byte = uart_rx_cfg->dma_buffer_rx[uart_rx_cfg->dma_buffer_rx_head];
    if (byte == 0) {
      // Hit delimiter
      // Get length
      uint len = sliding_window_length(uart_rx_cfg->dma_buffer_rx_tail,
                                       uart_rx_cfg->dma_buffer_rx_head,
                                       DMA_BUFFER_CAPACITY);

      // Start decoding data and advance tail to dma_buffer_head + 1
      if (len > 0) {
        // Copy all data into buffer
        static uint8_t cobs_buffer[COBS_MAX_SIZE_BYTES];
        uint cobs_index = 0;
        while (uart_rx_cfg->dma_buffer_rx_tail !=
               uart_rx_cfg->dma_buffer_rx_head) {
          cobs_buffer[cobs_index++] =
              uart_rx_cfg->dma_buffer_rx[uart_rx_cfg->dma_buffer_rx_tail];
          uart_rx_cfg->dma_buffer_rx_tail =
              (uart_rx_cfg->dma_buffer_rx_tail + 1) % DMA_BUFFER_CAPACITY;
        }

        // Advance tail by 1
        uart_rx_cfg->dma_buffer_rx_tail =
            (uart_rx_cfg->dma_buffer_rx_tail + 1) % DMA_BUFFER_CAPACITY;

        // Decode COBS into ethernet
        static uint8_t ethernet_frame_buffer[ETHERNET_FRAME_MAX_SIZE_BYTES];
        size_t decoded_len =
            cobs_decode(cobs_buffer, cobs_index, ethernet_frame_buffer,
                        ETHERNET_FRAME_MAX_SIZE_BYTES);
        if (decoded_len > ETHERNET_FRAME_MIN_SIZE_BYTES) {
          // CRC check
          uint8_t crc_lo = ethernet_frame_buffer[decoded_len - 2];
          uint8_t crc_hi = ethernet_frame_buffer[decoded_len - 1];
          uint16_t received_crc = ((uint16_t)crc_hi << 8) | crc_lo;
          uint16_t expected_crc =
              crc16(ethernet_frame_buffer, decoded_len - CRC_SIZE_BYTES);
          if (received_crc == expected_crc) {
            // CRC valid
            pb_istream_t stream = pb_istream_from_buffer(
                ethernet_frame_buffer + ETHERNET_HEADER_SIZE_BYTES,
                decoded_len - ETHERNET_HEADER_SIZE_BYTES - CRC_SIZE_BYTES);
            bool success = pb_decode(&stream, device_protocol_TCPSegment_fields,
                                     tcp_segment);

            // Debug
            // char str[256];
            // int offset = 0;
            // offset += snprintf(str + offset, sizeof(str) - offset,
            //                    "(pin %u) try_rcv_tcp_from_dma_buffer: %d, "
            //                    "%u, ",
            //                    pin, cobs_index, received_crc);
            // for (int i = 0; i < cobs_index && offset < sizeof(str); i++) {
            //   offset += snprintf(str + offset, sizeof(str) - offset,
            //   "%02X ",
            //                      cobs_buffer[i]);
            // }
            // snprintf(str + offset, sizeof(str) - offset,
            //          "(cobs_len, crc, cobs_data)");
            // LOG_DEBUG("%s", str);

            // Only return on success else continue draining
            if (success) {
              // Advance head by 1
              uart_rx_cfg->dma_buffer_rx_head =
                  (uart_rx_cfg->dma_buffer_rx_head + 1) % DMA_BUFFER_CAPACITY;
              return true;
            } else {
              LOG_INFO("(pin %u) try_rcv_tcp_from_dma_buffer: "
                       "decode failed as %s",
                       pin, PB_GET_ERROR(&stream));
            }
          } else {
            // CRC invalid
            // LOG_INFO(
            //     "(pin %u) try_rcv_tcp_from_dma_buffer: "
            //     "rcvd "
            //     "CRC %u != %u",
            //     pin, received_crc, expected_crc);
          }
        } else {
          // Undersized ethernet frame
          // LOG_INFO(
          //     "(pin %d) try_rcv_tcp_from_dma_buffer: could not decode
          //     COBS", pin);
        }
      } else {
        // LOG_INFO(
        //     "(pin %d) try_rcv_tcp_from_dma_buffer: delimiter too early",
        //     pin);
      }
    }
    // Advance head by 1
    uart_rx_cfg->dma_buffer_rx_head =
        (uart_rx_cfg->dma_buffer_rx_head + 1) % DMA_BUFFER_CAPACITY;
  }

  return false;
}

void send_tcp_over_socket(TCP_SOCKET *tcp_socket,
                          device_protocol_TCPSegment *tcp_segment) {

  UART_TX_CONFIG *uart_tx_config = &tcp_socket->uart_tx_cfg;
  if (tcp_socket->send_ack) {
    tcp_segment->ack = true;
    tcp_segment->ack_sequence = tcp_socket->tcp_segment_rwnd_tail;
  }

  // Initialize buffer and stream for encapsulating TCPSegment within our
  // EthernetFrame
  static uint8_t ethernet_frame_buffer[ETHERNET_FRAME_MAX_SIZE_BYTES];
  pb_ostream_t stream =
      pb_ostream_from_buffer(ethernet_frame_buffer + ETHERNET_HEADER_SIZE_BYTES,
                             sizeof(ethernet_frame_buffer) -
                                 ETHERNET_HEADER_SIZE_BYTES - CRC_SIZE_BYTES);

  // Write to stream
  mutex_enter_blocking(&uart_tx_config->mutex);
  if (!pb_encode(&stream, device_protocol_TCPSegment_fields, tcp_segment)) {
    panic("send_tcp_in_ethernet_in_cobs_in_uart: Failed to encode TCP segment");
  }

  // Write header
  uint8_t frame_len = ETHERNET_HEADER_SIZE_BYTES +
                      (uint8_t)stream.bytes_written + CRC_SIZE_BYTES;
  if (frame_len > ETHERNET_FRAME_MAX_SIZE_BYTES) {
    panic("send_tcp_in_ethernet_in_cobs_in_uart: Ethernet frame too large");
  }
  ethernet_frame_buffer[0] = 0;         // version
  ethernet_frame_buffer[1] = frame_len; // payload length

  // Write crc
  uint16_t crc = crc16(ethernet_frame_buffer, frame_len - CRC_SIZE_BYTES);
  ethernet_frame_buffer[frame_len - 2] = (uint8_t)(crc & 0xFF);
  ethernet_frame_buffer[frame_len - 1] = (uint8_t)((crc >> 8) & 0xFF);

  // Encode with COBS
  static uint8_t cobs_buffer[COBS_MAX_SIZE_BYTES + 1];
  size_t cobs_len = cobs_encode(ethernet_frame_buffer, frame_len, cobs_buffer,
                                sizeof(cobs_buffer));
  cobs_buffer[cobs_len++] = 0x00;

  // Push over UART
  put_uart_tx(uart_tx_config, cobs_buffer, cobs_len);
#if LOG_LEVEL >= LOG_LEVEL_DEBUG
  static char output[256];
  size_t offset = 0;

  offset += snprintf(output + offset, sizeof(output) - offset,
                     "send_tcp_in_ethernet_in_cobs_in_uart: %zu, %d, ",
                     cobs_len, crc);
  if (offset >= sizeof(output))
    offset = sizeof(output) - 1;

  for (size_t i = 0; i < cobs_len && offset < sizeof(output) - 1; i++) {
    offset += snprintf(output + offset, sizeof(output) - offset, "%02X ",
                       cobs_buffer[i]);
    if (offset >= sizeof(output))
      offset = sizeof(output) - 1;
  }

  snprintf(output + offset, sizeof(output) - offset,
           "(cobs_len, crc, cobs_data)");
  LOG_DEBUG("%s", output);
#endif
  tcp_socket->socket_prevent_timeout_at =
      make_timeout_time_ms(TCP_SOCKET_PREVENT_TIMEOUT_TIMER_MS);
  mutex_exit(&uart_tx_config->mutex);
  // mutex_enter_blocking(&uart_tx_config->mutex);
  // queue_into_data_into_dma_buffer(uart_tx_config, cobs_buffer, cobs_len);
  // mutex_exit(&uart_tx_config->mutex);
}

void transition_to_connected(TCP_SOCKET *tcp_socket, uint64_t to) {
  LOG_INFO("transition_to_connected: Socket connected %u, %u, %llu (rx_pin, "
           "tx_pin, to)",
           tcp_socket->uart_rx_cfg.pin, tcp_socket->uart_tx_cfg.pin,
           tcp_socket->to);
  tcp_socket->tcp_segment_swnd_head = 0;
  tcp_socket->tcp_segment_swnd_next = 0;
  tcp_socket->tcp_segment_swnd_tail = 0;
  tcp_socket->tcp_segment_rwnd_tail = 0;
  tcp_socket->send_ack = false;
  tcp_socket->to = to;
  tcp_socket->state = CONNECTED;
  if (tcp_socket->state == CONNECTED) {
    if (tcp_socket->on_disconnect) {
      tcp_socket->on_disconnect(tcp_socket->on_disconnect_arg);
    }
  } else if (tcp_socket->state == CONNECTING) {
  } else {
    panic("transition_to_connected: Unknown state");
  }
  if (tcp_socket->on_connect) {
    tcp_socket->on_connect(tcp_socket->on_connect_arg);
  }
}

void transition_to_connecting(TCP_SOCKET *tcp_socket) {
  LOG_INFO("transition_to_connected: Socket disconnected %u, %u, %llu (rx_pin, "
           "tx_pin, to)",
           tcp_socket->uart_rx_cfg.pin, tcp_socket->uart_tx_cfg.pin,
           tcp_socket->to);
  if (tcp_socket->state == CONNECTED) {
    if (tcp_socket->on_disconnect) {
      tcp_socket->on_disconnect(tcp_socket->on_disconnect_arg);
    }
    tcp_socket->state = CONNECTING;
  } else if (tcp_socket->state == CONNECTING) {
  } else {
    panic("transition_to_connecting: Unknown state");
  }
}

void rcv_task(void *arg) {
  TCP_SOCKET *tcp_socket = arg;
  mutex_enter_blocking(&tcp_socket->mutex);

  static device_protocol_TCPSegment tcp_segment =
      device_protocol_TCPSegment_init_zero;
  while (dma_chan_has_unprocessed_data(&tcp_socket->uart_rx_cfg)) {
    bool success =
        try_rcv_tcp_from_dma_buffer(&tcp_socket->uart_rx_cfg, &tcp_segment);
    if (success) {
      if (tcp_segment.syn) {
        // If SYN send SYN-ACK and perform state transition
        device_protocol_TCPSegment syn_ack =
            device_protocol_TCPSegment_init_zero;
        syn_ack.syn_ack = true;
        syn_ack.from = tcp_socket->from;
        syn_ack.to = tcp_segment.from;
        send_tcp_over_socket(tcp_socket, &syn_ack);
        transition_to_connected(tcp_socket, tcp_segment.from);
        LOG_INFO("rcv_task: Sent SYN-ACK to %llu", syn_ack.to);
      } else if (tcp_segment.to != tcp_socket->from) {
        // Case to catch unintended segments for this socket which covers a lot
        // of situations such as user plugging in a new device without a timeout
        // causing tcp_socket->state to switch to CONNECTING or using plugging
        // in a new device immediately after rcving a SYN packet from old device
        transition_to_connecting(tcp_socket);
        LOG_INFO("rcv_task: Rcvd unintended segments for socket %llu, %llu "
                 "(intended, "
                 "actual)",
                 tcp_segment.to, tcp_socket->from);
        log_info_tcp_segment(&tcp_segment);

      } else if (tcp_segment.syn_ack) {
        transition_to_connected(
            tcp_socket,
            tcp_segment.from); // No matter what will want to transition as
        // opposing device as resetted socket states
        LOG_DEBUG("rcv_task: Rcvd SYN-ACK from %llu", tcp_segment.from);
      } else if (tcp_socket->state == CONNECTING) {
        // Above SYN-ACK case already covers switching to CONNECTED
        // Leaving this case empty also ignores all packets and protects rwnd
        // incase the other device is CONNECTED but this device is CONNECTING
      } else if (tcp_socket->state == CONNECTED) {
        // ACK (concerns only swnd)
        if (tcp_segment.ack) {
          if (is_after_left_and_before_or_eq_right_sliding_window(
                  tcp_socket->tcp_segment_swnd_tail, tcp_segment.ack_sequence,
                  tcp_socket->tcp_segment_swnd_next)) {
            while (tcp_socket->tcp_segment_swnd_tail !=
                   tcp_segment.ack_sequence) {
              if (tcp_socket->on_rcv[tcp_socket->tcp_segment_swnd_tail]) {
                tcp_socket->on_rcv[tcp_socket->tcp_segment_swnd_tail](
                    tcp_socket->on_rcv_arg[tcp_socket->tcp_segment_swnd_tail]);
                tcp_socket->on_rcv[tcp_socket->tcp_segment_swnd_tail] = NULL;
                tcp_socket->on_rcv_arg[tcp_socket->tcp_segment_swnd_tail] =
                    NULL;
              }
              tcp_socket->tcp_segment_swnd_tail =
                  (tcp_socket->tcp_segment_swnd_tail + 1) %
                  TCP_SEGMENT_BUFFER_CAPACITY;
            }
            LOG_INFO(
                "rcv_task: Rcvd ACK %u, %u, %u (ack_seq/swnd_tail, swnd_next, "
                "swnd_head)",
                tcp_socket->tcp_segment_swnd_tail,
                tcp_socket->tcp_segment_swnd_next,
                tcp_socket->tcp_segment_swnd_head);
          } else {
            LOG_INFO("rcv_task: Rcvd stale ACK %u, %u (ack_seq, swnd_tail)",
                     tcp_segment.ack_sequence,
                     tcp_socket->tcp_segment_swnd_tail);
          }
        }

        // Handle application layer packet (concerns only rwnd)
        if (tcp_segment.has_application_layer_packet) {
          if (is_at_least_left_and_before_right_sliding_window(
                  tcp_socket->tcp_segment_rwnd_tail, tcp_segment.sequence,
                  (tcp_socket->tcp_segment_rwnd_tail + tcp_socket->cwnd) %
                      TCP_SEGMENT_BUFFER_CAPACITY)) {
            if (tcp_socket->tcp_segment_available[tcp_segment.sequence]) {
              LOG_INFO(
                  "rcv_task: Rcvd duplicate packet %u, %u (seq, rwnd_tail)",
                  tcp_segment.sequence, tcp_socket->tcp_segment_rwnd_tail);
            } else {
              // Store first into rwnd buffer and mark as available
              tcp_socket->tcp_segment_available[tcp_segment.sequence] = true;
              tcp_socket->tcp_segment_rwnd_buffer[tcp_segment.sequence] =
                  tcp_segment;

              // Consume available segment at rwnd tail and increase rwnd tail
              while (tcp_socket->tcp_segment_available
                         [tcp_socket->tcp_segment_rwnd_tail]) {
                if (tcp_socket->app_packet_handler) {
                  tcp_socket->app_packet_handler(
                      &tcp_segment.application_layer_packet);
                }
                tcp_socket
                    ->tcp_segment_available[tcp_socket->tcp_segment_rwnd_tail] =
                    false;
                tcp_socket->tcp_segment_rwnd_tail =
                    (tcp_socket->tcp_segment_rwnd_tail + 1) %
                    TCP_SEGMENT_BUFFER_CAPACITY;
                tcp_socket->send_ack =
                    true; // Offload to swnd_sender_task or
                          // timeout_if_connected_and_send_syn_if_unconnected_task
                          // to send ACK
              }
              LOG_INFO("rcv_task: Rcvd new packet %u, %u (seq, rwnd_tail)",
                       tcp_segment.sequence, tcp_socket->tcp_segment_rwnd_tail);
            }
          } else {
            LOG_INFO("rcv_task: Rcvd stale packet %u, %u (seq, rwnd_tail)",
                     tcp_segment.sequence, tcp_socket->tcp_segment_rwnd_tail);
          }
        }
      } else {
        panic("rcv_task: Unknown state");
      }
    } else {
      LOG_DEBUG("rcv_task: Failed to rcv TCP segment");
    }
    tcp_socket->socket_timeout_at =
        make_timeout_time_ms(TCP_SOCKET_TIMEOUT_TIMER_MS);
  }

  mutex_exit(&tcp_socket->mutex);
  core1_task_queue_post(rcv_task, tcp_socket);
}

void swnd_sender_task(void *arg) {
  TCP_SOCKET *tcp_socket = arg;

  if (tcp_socket->state == CONNECTING) {
    // Any TCP segments encapsulating our application layer packets are
    // invalidated by this state as these application layer packets are meant
    // for the previous device
  } else if (tcp_socket->state == CONNECTED) {
    mutex_enter_blocking(&tcp_socket->mutex);

    // Timeouts for un-ACK-ed segments
    if (tcp_socket->tcp_segment_swnd_tail !=
        tcp_socket->tcp_segment_swnd_next) {
      int64_t diff = absolute_time_diff_us(get_absolute_time(),
                                           tcp_socket->segment_timeout_at);
      if (diff <= 0) {
        // Timeout! Send all segments
        int tcp_segment_swnd_tail = tcp_socket->tcp_segment_swnd_tail;
        while (tcp_segment_swnd_tail != tcp_socket->tcp_segment_swnd_next) {
          device_protocol_TCPSegment *tcp_segment =
              &tcp_socket->tcp_segment_swnd_buffer[tcp_segment_swnd_tail];
          send_tcp_over_socket(tcp_socket, tcp_segment);
          tcp_segment_swnd_tail =
              (tcp_segment_swnd_tail + 1) % TCP_SEGMENT_BUFFER_CAPACITY;
        }
        if (tcp_socket->send_ack) {
          LOG_INFO(
              "swnd_sender_task: Resent with ACK %u; %u, %u, %u (rwnd_tail; "
              "swnd_tail, "
              "swnd_next, "
              "swnd_head)",
              tcp_socket->tcp_segment_rwnd_tail,
              tcp_socket->tcp_segment_swnd_tail,
              tcp_socket->tcp_segment_swnd_next,
              tcp_socket->tcp_segment_swnd_head);
        } else {
          LOG_INFO("swnd_sender_task: Resent %u, %u, %u (swnd_tail, "
                   "swnd_next, "
                   "swnd_head)",
                   tcp_socket->tcp_segment_swnd_tail,
                   tcp_socket->tcp_segment_swnd_next,
                   tcp_socket->tcp_segment_swnd_head);
        }
        tcp_socket->segment_timeout_at =
            make_timeout_time_ms(TCP_SEGMENT_TIMEOUT_TIMER_MS);
      } else {
        // LOG_INFO("swnd_sender_task: diff = %lld us", diff);
      }
    } else {
      LOG_DEBUG("swnd_sender_task: no pending ACK packets %u, "
                "%u, "
                "%u (tail, next, "
                "head)",
                tcp_socket->tcp_segment_swnd_tail,
                tcp_socket->tcp_segment_swnd_next,
                tcp_socket->tcp_segment_swnd_head);
    }

    // Check if unsent segments present in queue
    if (tcp_socket->tcp_segment_swnd_next !=
        tcp_socket->tcp_segment_swnd_head) {
      bool sent = false;
      while (sliding_window_length(tcp_socket->tcp_segment_swnd_tail,
                                   tcp_socket->tcp_segment_swnd_next,
                                   TCP_SEGMENT_BUFFER_CAPACITY) <
             tcp_socket->cwnd) {
        // Not congested
        device_protocol_TCPSegment *unsent_segment =
            &tcp_socket
                 ->tcp_segment_swnd_buffer[tcp_socket->tcp_segment_swnd_next];
        send_tcp_over_socket(tcp_socket, unsent_segment);
        if (unsent_segment->ack) {
          LOG_DEBUG("swnd_sender_task: Sent TCP segment %u with ACK %u",
                    unsent_segment->sequence, unsent_segment->ack_sequence);
        } else {
          LOG_DEBUG("swnd_sender_task: Sent TCP segment %u",
                    unsent_segment->sequence);
        }
        sent = true;
        tcp_socket->tcp_segment_swnd_next =
            (tcp_socket->tcp_segment_swnd_next + 1) %
            TCP_SEGMENT_BUFFER_CAPACITY;
      }
      if (sent) {
        if (tcp_socket->send_ack) {
          LOG_INFO("swnd_sender_task: Sent with ACK %u, %u, %u, %u (ack_seq, "
                   "swnd_tail, "
                   "swnd_next, "
                   "swnd_head)",
                   tcp_socket->tcp_segment_rwnd_tail,
                   tcp_socket->tcp_segment_swnd_tail,
                   tcp_socket->tcp_segment_swnd_next,
                   tcp_socket->tcp_segment_swnd_head);
        } else {
          LOG_INFO("swnd_sender_task: Sent %u, %u, %u (swnd_tail, "
                   "swnd_next, "
                   "swnd_head)",
                   tcp_socket->tcp_segment_swnd_tail,
                   tcp_socket->tcp_segment_swnd_next,
                   tcp_socket->tcp_segment_swnd_head);
        }
        tcp_socket->segment_timeout_at =
            make_timeout_time_ms(TCP_SEGMENT_TIMEOUT_TIMER_MS);
      } else {
        LOG_DEBUG("swnd_sender_task: Congested %u, %u, %u (tail, "
                  "next, head)",
                  tcp_socket->tcp_segment_swnd_tail,
                  tcp_socket->tcp_segment_swnd_next,
                  tcp_socket->tcp_segment_swnd_head);
      }
    } else {
      LOG_DEBUG("swnd_sender_task: No segments to send %u, %u, "
                "%u "
                "(tail, next, head)",
                tcp_socket->tcp_segment_swnd_tail,
                tcp_socket->tcp_segment_swnd_next,
                tcp_socket->tcp_segment_swnd_head);
    }

    mutex_exit(&tcp_socket->mutex);
  } else {
    panic("swnd_sender_task: Unknown state");
  }

  core1_task_queue_post(swnd_sender_task, tcp_socket);
}

void timeout_if_connected_and_send_syn_if_unconnected_task(void *arg) {
  TCP_SOCKET *tcp_socket = arg;
  mutex_enter_blocking(&tcp_socket->mutex);

  if (tcp_socket->state == CONNECTED) {
    int64_t socket_timeout_diff = absolute_time_diff_us(
        get_absolute_time(), tcp_socket->socket_timeout_at);
    if (socket_timeout_diff <= 0) {
      transition_to_connecting(tcp_socket);
      LOG_DEBUG("timeout_if_connected_and_send_syn_if_unconnected_task: Socket "
                "timeout");
    } else {
      int64_t socket_prevent_timeout_diff = absolute_time_diff_us(
          get_absolute_time(), tcp_socket->socket_prevent_timeout_at);
      if (socket_prevent_timeout_diff <= 0) {
        device_protocol_TCPSegment prevent_timeout_segment =
            device_protocol_TCPSegment_init_zero;
        prevent_timeout_segment.from = tcp_socket->from;
        prevent_timeout_segment.to = tcp_socket->to;
        send_tcp_over_socket(tcp_socket, &prevent_timeout_segment);
        LOG_DEBUG("timeout_if_connected_and_send_syn_if_unconnected_task: "
                  "Preventing "
                  "socket timeout on "
                  "other end");
      } else {
        LOG_DEBUG(
            "timeout_if_connected_and_send_syn_if_unconnected_task: %lld, "
            "%lld, (timeout, prevent_timeout)",
            socket_timeout_diff, socket_prevent_timeout_diff);
      }
    }
  } else if (tcp_socket->state == CONNECTING) {
    device_protocol_TCPSegment syn_segment =
        device_protocol_TCPSegment_init_zero;
    syn_segment.from = tcp_socket->from;
    syn_segment.syn = true;
    send_tcp_over_socket(tcp_socket, &syn_segment);
    LOG_DEBUG(
        "timeout_if_connected_and_send_syn_if_unconnected_task: Sent SYN");
  } else {
    panic("timeout_if_connected_and_send_syn_if_unconnected_task: Unknown "
          "state");
  }

  mutex_exit(&tcp_socket->mutex);
  core1_task_queue_post(timeout_if_connected_and_send_syn_if_unconnected_task,
                        tcp_socket);
}

bool async_send_app_packet_in_tcp(
    TCP_SOCKET *tcp_socket,
    device_protocol_ApplicationLayerPacket *application_layer_packet,
    on_rcv_t on_rcv, void *on_rcv_arg) {
  if (tcp_socket->state != CONNECTED) {
    LOG_DEBUG("async_send_app_packet_in_tcp: Not connected");
    return false;
  }

  // Check if queue is empty
  uint8_t next_head =
      (tcp_socket->tcp_segment_swnd_head + 1) % TCP_SEGMENT_BUFFER_CAPACITY;
  if (next_head == tcp_socket->tcp_segment_swnd_tail) {
    LOG_DEBUG(
        "async_send_app_packet_in_tcp: Queue full; %u, %u, %u (tail, next, "
        "head)",
        tcp_socket->tcp_segment_swnd_tail, tcp_socket->tcp_segment_swnd_next,
        tcp_socket->tcp_segment_swnd_head);
    return false;
  }

  // Modify segment in buffer and increase head
  mutex_enter_blocking(&tcp_socket->mutex);
  device_protocol_TCPSegment *segment =
      &tcp_socket->tcp_segment_swnd_buffer[tcp_socket->tcp_segment_swnd_head];
  segment->application_layer_packet = *application_layer_packet;
  segment->has_application_layer_packet = true;
  segment->sequence = tcp_socket->tcp_segment_swnd_head;
  segment->ack = false;
  segment->from = tcp_socket->from;
  segment->to = tcp_socket->to;
  tcp_socket->tcp_segment_swnd_head = next_head;
  tcp_socket->on_rcv[tcp_socket->tcp_segment_swnd_head] = on_rcv;
  tcp_socket->on_rcv_arg[tcp_socket->tcp_segment_swnd_head] = on_rcv_arg;
  mutex_exit(&tcp_socket->mutex);

  LOG_DEBUG(
      "async_send_app_packet_in_tcp: Queued %u, %u, %u (tail, next, head)",
      tcp_socket->tcp_segment_swnd_tail, tcp_socket->tcp_segment_swnd_next,
      tcp_socket->tcp_segment_swnd_head);
  return true;
}

TCP_SOCKET *init_socket(TCP_SOCKET_CFG *tcp_socket_cfg) {
  if (sockets_initialized >= MAX_TCP_SOCKETS)
    panic("init_socket: Cannot support more than %u sockets on this hardware",
          MAX_TCP_SOCKETS);

  TCP_SOCKET *tcp_socket = &tcp_sockets[sockets_initialized];
  tcp_socket->cwnd = INITIAL_CWND;
  if (tcp_socket->cwnd <= 0) {
    panic("init_socket: Invalid cwnd value");
  }

  // Initialize RX;
  tcp_socket->uart_rx_cfg = (UART_RX_CONFIG){
      .pin = tcp_socket_cfg->rx_pin,
      .pio = tcp_socket_cfg->rx_pio,
      .sm = tcp_socket_cfg->rx_sm,
  };
  uart_rx_pio_on_gpio(&tcp_socket->uart_rx_cfg);

  // Initialize TX
  tcp_socket->uart_tx_cfg = (UART_TX_CONFIG){.pin = tcp_socket_cfg->tx_pin,
                                             .pio = tcp_socket_cfg->tx_pio,
                                             .sm = tcp_socket_cfg->tx_sm};
  uart_tx_pio_on_gpio(&tcp_socket->uart_tx_cfg);

  // Initialize mutex
  mutex_init(&tcp_socket->mutex);

  // Initialize tasks
  core1_task_queue_post(swnd_sender_task, tcp_socket);
  core1_task_queue_post(timeout_if_connected_and_send_syn_if_unconnected_task,
                        tcp_socket);
  core1_task_queue_post(rcv_task, tcp_socket);

  sockets_initialized++;
  return tcp_socket;
}
