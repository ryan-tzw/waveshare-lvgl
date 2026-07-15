#ifndef SOCKET_H
#define SOCKET_H

#include "core1_task_queue.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "pio.h"
#include "protocol.pb.h"
#include "pico/sync.h"
#include "cobs.h"

typedef void (*app_packet_handler)(device_protocol_ApplicationLayerPacket* application_layer_packet);
typedef void (*async_worker_func_t)(
    async_context_t* async_context,
    async_when_pending_worker_t* worker
);

/**
 * Guarantees: on timeout, on receive, ordering
 */
typedef struct TCP_SOCKET_CFG {
  uint rx_pin;
  uint tx_pin;
  async_worker_func_t answer_all_tcp_wrapper;
  void (*socket_timeout_handler)(void* tcp_socket);
} TCP_SOCKET_CFG;

#define TCP_SEGMENT_BUFFER_CAPACITY 16
#define CWND 1

typedef struct TCP_SOCKET {
  UART_RX_CONFIG uart_rx_cfg;
  UART_TX_CONFIG uart_tx_cfg;

  uint8_t cwnd;

  device_protocol_TCPSegment tcp_segment_swnd_buffer[TCP_SEGMENT_BUFFER_CAPACITY];
  core1_task_fn_t on_rcv[TCP_SEGMENT_BUFFER_CAPACITY];
  void* on_rcv_arg[TCP_SEGMENT_BUFFER_CAPACITY];
  volatile uint8_t tcp_segment_swnd_tail;
  volatile uint8_t tcp_segment_swnd_next;
  volatile uint8_t tcp_segment_swnd_head;
  volatile int32_t segment_timeout_timer;

  device_protocol_TCPSegment tcp_segment_rwnd_buffer[TCP_SEGMENT_BUFFER_CAPACITY];
  bool tcp_segment_available[TCP_SEGMENT_BUFFER_CAPACITY];
  volatile uint8_t tcp_segment_rwnd_tail;
  volatile uint8_t tcp_segment_rwnd_head;
  app_packet_handler app_packet_handler;
  volatile bool send_ack;

  volatile int32_t socket_timeout_timer; // Decays to 0 and only rejuvenated whenever socket receives a segment
  volatile int32_t socket_prevent_timeout_timer; // Decays to 0 and only rejuvenated 
  void (*socket_timeout_handler)(void* tcp_socket);

  mutex_t mutex;
} TCP_SOCKET;

TCP_SOCKET* init_socket(TCP_SOCKET_CFG* tcp_socket_cfg);

bool async_send_app_packet_in_tcp(
    TCP_SOCKET* tcp_socket,
    device_protocol_ApplicationLayerPacket* application_layer_packet,
    core1_task_fn_t on_rcv, void* on_rcv_arg, core1_task_fn_t on_timeout,
    void* on_timeout_arg);

void answer_all_tcp(TCP_SOCKET* tcp_socket);

#endif