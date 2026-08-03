#include "simulator_engine.h"

#include "logging.h"
#include "protocol.pb.h"

#define CONNECTED_SIMULATOR_ENGINES_MAX 4
static CONNECTED_SIMULATOR_ENGINE
    connected_simulator_engines[CONNECTED_SIMULATOR_ENGINES_MAX];

static device_protocol_ApplicationLayerPacket app_packet;

static void on_connect(void *tcp_socket) { LOG_INFO("on_connect: Connected"); }
static void on_disconnect(void *tcp_socket) {
  LOG_INFO("on_disconnect: Disconnected");
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

void init_simulator_engine(void) {
  int pin_mappings[CONNECTED_SIMULATOR_ENGINES_MAX][2] = {
      {28, 29}, {3, 0}, {8, 22}, {6, 5}}; // {{RX, TX}, ...}
  PIO pios[CONNECTED_SIMULATOR_ENGINES_MAX][2] = {
      {pio0, pio0}, {pio0, pio0}, {pio1, pio1}, {pio1, pio1}};
  uint sms[CONNECTED_SIMULATOR_ENGINES_MAX][2] = {
      {0, 1}, {2, 3}, {0, 1}, {2, 3}};
  for (int i = 0; i < CONNECTED_SIMULATOR_ENGINES_MAX; i++) {
    TCP_SOCKET_CFG tcp_socket_cfg = {.rx_pin = pin_mappings[i][0],
                                     .tx_pin = pin_mappings[i][1],
                                     .rx_pio = pios[i][0],
                                     .rx_sm = sms[i][0],
                                     .tx_pio = pios[i][1],
                                     .tx_sm = sms[i][1]};
    TCP_SOCKET *tcp_socket = init_socket(&tcp_socket_cfg);
    tcp_socket->on_connect = on_connect;
    tcp_socket->on_disconnect = on_disconnect;
    tcp_socket->app_packet_handler = app_packet_handler_func;
    connected_simulator_engines[i].tcp_socket = tcp_socket;
  }
};