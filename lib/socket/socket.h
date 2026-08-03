#ifndef SOCKET_H
#define SOCKET_H

#include "pio.h"
#include "protocol.pb.h"

typedef void (*app_packet_handler)(
    device_protocol_ApplicationLayerPacket *application_layer_packet);

/**
 * Guarantees: on timeout, on receive, ordering
 */
typedef struct TCP_SOCKET_CFG {
  uint rx_pin;
  uint tx_pin;
  PIO rx_pio;
  uint rx_sm;
  PIO tx_pio;
  uint tx_sm;
  void (*socket_timeout_handler)(void *tcp_socket);
} TCP_SOCKET_CFG;

#define TCP_SEGMENT_BUFFER_CAPACITY 16
#define INITIAL_CWND 1

#define ETHERNET_HEADER_SIZE_BYTES 2
#define CRC_SIZE_BYTES 2
#define ETHERNET_FRAME_MIN_SIZE_BYTES                                          \
  (ETHERNET_HEADER_SIZE_BYTES + CRC_SIZE_BYTES)
#define ETHERNET_FRAME_MAX_SIZE_BYTES 128
#define ETHERNET_PAYLOAD_MAX_SIZE_BYTES                                        \
  (ETHERNET_FRAME_MAX_SIZE_BYTES - ETHERNET_HEADER_SIZE_BYTES - CRC_SIZE_BYTES)
#define COBS_MAX_SIZE_BYTES                                                    \
  (ETHERNET_FRAME_MAX_SIZE_BYTES + (ETHERNET_FRAME_MAX_SIZE_BYTES / 254) + 2)

#define TCP_SEGMENT_TIMEOUT_TIMER_MS 2048
#define TCP_SOCKET_TIMEOUT_TIMER_MS 8192
#define TCP_SOCKET_PREVENT_TIMEOUT_TIMER_MS 4096

typedef enum TCPSocketState { CONNECTING, CONNECTED } TCPSocketState;

typedef void (*on_rcv_t)(void *arg);

typedef struct TCP_SOCKET {
  TCPSocketState state;

  UART_RX_CONFIG uart_rx_cfg;
  UART_TX_CONFIG uart_tx_cfg;

  uint64_t from;
  uint64_t to;

  uint8_t cwnd;

  device_protocol_TCPSegment
      tcp_segment_swnd_buffer[TCP_SEGMENT_BUFFER_CAPACITY];
  on_rcv_t on_rcv[TCP_SEGMENT_BUFFER_CAPACITY];
  void *on_rcv_arg[TCP_SEGMENT_BUFFER_CAPACITY];
  volatile uint8_t tcp_segment_swnd_tail;
  volatile uint8_t tcp_segment_swnd_next;
  volatile uint8_t tcp_segment_swnd_head;
  volatile absolute_time_t segment_timeout_at;

  device_protocol_TCPSegment
      tcp_segment_rwnd_buffer[TCP_SEGMENT_BUFFER_CAPACITY];
  bool tcp_segment_available[TCP_SEGMENT_BUFFER_CAPACITY];
  volatile uint8_t tcp_segment_rwnd_tail;
  app_packet_handler app_packet_handler;
  volatile bool send_ack;

  volatile absolute_time_t socket_timeout_at;
  volatile absolute_time_t socket_prevent_timeout_at;

  void (*on_connect)(void *on_connect_arg);
  void *on_connect_arg;
  void (*on_disconnect)(void *on_disconnect_arg);
  void *on_disconnect_arg;

  mutex_t mutex;
} TCP_SOCKET;

TCP_SOCKET *init_socket(TCP_SOCKET_CFG *tcp_socket_cfg);

bool async_send_app_packet_in_tcp(
    TCP_SOCKET *tcp_socket,
    device_protocol_ApplicationLayerPacket *application_layer_packet,
    on_rcv_t on_rcv, void *on_rcv_arg);

#endif