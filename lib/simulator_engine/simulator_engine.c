#include "simulator_engine.h"

#include "core1_task_queue.h"
#include "logging.h"
#include "pico/unique_id.h"
#include "protocol.pb.h"
#include "simulator_engine_map.h"
#include "socket.h"
#include <pico/types.h>

static DIRECTLY_CONNECTED_SIMULATOR_ENGINE directly_connected_simulator_engines
    [DIRECTLY_CONNECTED_SIMULATOR_ENGINES_MAX];
static SIMULATOR_ENGINE *self_ptr;

static void on_connect(void *arg) {
  TCP_SOCKET *self = arg;
  LOG_INFO("on_connect: Connected to %llu", self->to);
}
static void on_disconnect(void *arg) {
  TCP_SOCKET *self = arg;
  LOG_INFO("on_disconnect: Disconnected from %llu", self->to);
}
static void on_rcv(void *nullptr) { LOG_INFO("on_rcv: Opposing device rcvd"); }
static void
app_packet_handler_func(device_protocol_ApplicationLayerPacket *app_packet) {
  LOG_INFO(
      "app_packet_handler_func: ApplicationLayerPacket { component = %s (%d) "
      "}",
      app_packet->component ==
              device_protocol_ApplicationLayerPacket_Component_BATTERY
          ? "BATTERY"
      : app_packet->component ==
              device_protocol_ApplicationLayerPacket_Component_BULB
          ? "BULB"
          : "UNKNOWN",
      app_packet->component);
}

static void core1_main(void *arg) {
  static device_protocol_ApplicationLayerPacket app_packet;
  static absolute_time_t send_at;
  int64_t diff = absolute_time_diff_us(get_absolute_time(), send_at);
  if (diff <= 0) {
    app_packet.component =
        device_protocol_ApplicationLayerPacket_Component_BULB;
    async_send_app_packet_in_tcp(
        directly_connected_simulator_engines[0].tcp_socket, &app_packet, on_rcv,
        NULL);
    send_at = make_timeout_time_ms(1000);
  }

  core1_task_queue_post(core1_main, NULL);
}

void put_simulator_engine(key_t key, SIMULATOR_ENGINE *simulator_engine) {
#define MAX_SIMULATOR_ENGINES 64
  static SIMULATOR_ENGINE simulator_engine_heap[MAX_SIMULATOR_ENGINES];
  static size_t simulator_engine_next = 0;
  static bool simulator_engine_present[MAX_SIMULATOR_ENGINES] = {false};

  for (size_t i = 0; i < MAX_SIMULATOR_ENGINES; i++) {
    // malloc using our own heap
    if (simulator_engine_present[simulator_engine_next]) {
      simulator_engine_next =
          (simulator_engine_next + 1) % MAX_SIMULATOR_ENGINES;
      continue;
    }

    // Copy into heap and store reference into map
    simulator_engine_heap[simulator_engine_next] = *simulator_engine;
    put_into_simulator_engine_map(
        key, &simulator_engine_heap[simulator_engine_next]);

    // Mark as not free
    simulator_engine_present[simulator_engine_next] = true;
  }
  panic("insert_simulator_engine: Ran out of space");
}

void init_simulator_engine(void) {
  // Get ID
  pico_unique_board_id_t id;
  pico_get_unique_board_id(&id);
  uint64_t uint64_id = 0;
  for (int i = 0; i < 8; i++) {
    uint64_id |= ((uint64_t)id.id[i]) << (8 * i);
  }

  // Hash table (Adjacency array) of all simulator engines
  init_simulator_engine_map();
  SIMULATOR_ENGINE self = {.id = uint64_id};
  put_simulator_engine(self.id, &self);

  // Initialize sockets
  int pin_mappings[DIRECTLY_CONNECTED_SIMULATOR_ENGINES_MAX][2] = {
      {28, 29}, {3, 0}, {8, 22}, {6, 5}}; // {{RX, TX}, ...}
  PIO pios[DIRECTLY_CONNECTED_SIMULATOR_ENGINES_MAX][2] = {
      {pio0, pio0}, {pio0, pio0}, {pio1, pio1}, {pio1, pio1}};
  uint sms[DIRECTLY_CONNECTED_SIMULATOR_ENGINES_MAX][2] = {
      {0, 1}, {2, 3}, {0, 1}, {2, 3}};
  for (int i = 0; i < DIRECTLY_CONNECTED_SIMULATOR_ENGINES_MAX; i++) {
    TCP_SOCKET_CFG tcp_socket_cfg = {.rx_pin = pin_mappings[i][0],
                                     .tx_pin = pin_mappings[i][1],
                                     .rx_pio = pios[i][0],
                                     .rx_sm = sms[i][0],
                                     .tx_pio = pios[i][1],
                                     .tx_sm = sms[i][1]};
    TCP_SOCKET *tcp_socket = init_socket(&tcp_socket_cfg);
    tcp_socket->on_connect = on_connect;
    tcp_socket->on_connect_arg = tcp_socket;
    tcp_socket->on_disconnect = on_disconnect;
    tcp_socket->on_disconnect_arg = tcp_socket;
    tcp_socket->app_packet_handler = app_packet_handler_func;
    tcp_socket->from = uint64_id;
    directly_connected_simulator_engines[i].tcp_socket = tcp_socket;
  }
  core1_task_queue_post(core1_main, NULL);
};