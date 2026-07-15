#include "simulator_engine.h"

#define CONNECTED_SIMULATOR_ENGINES_MAX 4
static CONNECTED_SIMULATOR_ENGINE connected_simulator_engines[CONNECTED_SIMULATOR_ENGINES_MAX];
#define ANSWER_ALL_TCP_DEFINE(n) \
static void async_worker_##n(async_context_t *async_context, async_when_pending_worker_t *worker) { \
    printf("async_worker: called for pin %d\n", connected_simulator_engines[n].tcp_socket->uart_rx_cfg.pin);\
    answer_all_tcp(connected_simulator_engines[n].tcp_socket); \
}
#define FOR_EACH(X) \
    X(0) X(1) X(2) X(3)
#define ENTRY(n) async_worker_##n,
FOR_EACH(ANSWER_ALL_TCP_DEFINE)
static void (*answer_all_tcp_wrappers[4])(async_context_t *async_context, async_when_pending_worker_t *worker) = {
    FOR_EACH(ENTRY)
};

static device_protocol_ApplicationLayerPacket app_packet;

static void core1_main(void* arg) {
    sleep_ms(1000);
    app_packet.component = device_protocol_ApplicationLayerPacket_Component_BULB;
    bool success = async_send_app_packet_in_tcp(connected_simulator_engines[0].tcp_socket, &app_packet, NULL, NULL, NULL, NULL);
    printf("core1_main: %u\n", success);
    core1_task_queue_post(core1_main, NULL);
}

static void core2_main(void* arg) {
    app_packet.component = device_protocol_ApplicationLayerPacket_Component_BULB;
    bool success = async_send_app_packet_in_tcp(connected_simulator_engines[1].tcp_socket, &app_packet, NULL, NULL, NULL, NULL);
    printf("core2_main: %u\n", success);
    core1_task_queue_post(core1_main, NULL);
}

static void core3_main(void* arg) {
    app_packet.component = device_protocol_ApplicationLayerPacket_Component_BULB;
    bool success = async_send_app_packet_in_tcp(connected_simulator_engines[2].tcp_socket, &app_packet, NULL, NULL, NULL, NULL);
    printf("core3_main: %u\n", success);
    core1_task_queue_post(core1_main, NULL);
}

static void core4_main(void* arg) {
    app_packet.component = device_protocol_ApplicationLayerPacket_Component_BULB;
    bool success = async_send_app_packet_in_tcp(connected_simulator_engines[3].tcp_socket, &app_packet, NULL, NULL, NULL, NULL);
    printf("core4_main: %u\n", success);
    core1_task_queue_post(core1_main, NULL);
}

void init_simulator_engine(void) {
    // // Map the irq functions manually
    // connected_simulator_engines[0].rx_cfg.pio_irq_func_wrapper = &uart_rx_pio_irq_func_wrapper_0;
    // connected_simulator_engines[1].rx_cfg.pio_irq_func_wrapper = &uart_rx_pio_irq_func_wrapper_1;
    // connected_simulator_engines[2].rx_cfg.pio_irq_func_wrapper = &uart_rx_pio_irq_func_wrapper_2;
    // connected_simulator_engines[3].rx_cfg.pio_irq_func_wrapper = &uart_rx_pio_irq_func_wrapper_3;

    // // Map the rest of it
    // int pin_mappings[CONNECTED_SIMULATOR_ENGINES_MAX][2] = {{28, 29}, {3, 0}, {8, 22}, {6, 5}}; // {{RX, TX}, ...}
    // for (int i=0; i<CONNECTED_SIMULATOR_ENGINES_MAX; i++) {
    //     // RX init
    //     connected_simulator_engines[i].rx_cfg.pin = pin_mappings[i][0]; 
    //     connected_simulator_engines[i].rx_cfg.worker.do_work = async_worker_func;
    //     uart_rx_pio_on_gpio(&connected_simulator_engines[i].rx_cfg);

    //     // TX init
    //     connected_simulator_engines[i].tx_cfg.pin = pin_mappings[i][1];
    //     uart_tx_pio_on_gpio(&connected_simulator_engines[i].tx_cfg);
    // }

    int pin_mappings[CONNECTED_SIMULATOR_ENGINES_MAX][2] = {{28, 29}, {3, 0}, {8, 22}, {6, 5}}; // {{RX, TX}, ...}
    for (int i=0; i<CONNECTED_SIMULATOR_ENGINES_MAX - 3; i++) {
        TCP_SOCKET_CFG tcp_socket_cfg = { .rx_pin = pin_mappings[i][0], .tx_pin = pin_mappings[i][1]};
        tcp_socket_cfg.answer_all_tcp_wrapper = answer_all_tcp_wrappers[i];
        TCP_SOCKET* tcp_socket = init_socket(&tcp_socket_cfg);
        connected_simulator_engines[i].tcp_socket = tcp_socket; 
    }
    core1_task_queue_post(core1_main, NULL);
    // core1_task_queue_post(core2_main, NULL);
    // core1_task_queue_post(core3_main, NULL);
    // core1_task_queue_post(core4_main, NULL);

};