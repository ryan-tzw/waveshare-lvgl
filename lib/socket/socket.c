#include "socket.h"

#define MAX_TCP_SOCKETS 4
static uint sockets_initialized = 0;
static TCP_SOCKET tcp_sockets[MAX_TCP_SOCKETS];
#define UART_RX_WRAPPER_DEFINE(n) \
static void uart_rx_pio_irq_func_wrapper_##n(void) { \
    uart_rx_pio_irq_func(&tcp_sockets[n].uart_rx_cfg); \
}
#define FOR_EACH(X) \
    X(0) X(1) X(2) X(3)
#define ENTRY(n) uart_rx_pio_irq_func_wrapper_##n,
FOR_EACH(UART_RX_WRAPPER_DEFINE)
static void (*pio_irq_func_wrappers[4])(void) = {
    FOR_EACH(ENTRY)
};

int slide_to_right(uint8_t position, uint8_t distance, uint8_t capacity) {
    int result = (position + distance) % capacity;
    // printf("slide_to_right: (%u + %u) mod %u = %d\n", position, distance, capacity, result);
    return result;
}

bool is_after_left_and_before_or_eq_right_sliding_window(uint8_t left, uint8_t position, uint8_t right) {
    if (left <= right) {
        return left < position && position <= right;
    } else {
        return !(right < position && position <= left); 
    }
}

bool is_at_least_left_and_before_right_sliding_window(uint8_t left, uint8_t position, uint8_t right) {
    if (left <= right) {
        return left <= position && position < right;
    } else {
        return !(right <= position && position < left); 
    }
}

int left_to_right_distance_sliding_window(uint8_t left, uint8_t right, uint8_t capacity) {
    if (left <= right) {
        return right - left;
    } else {
        return (capacity - left) + right;
    }
}

bool socket_is_empty(TCP_SOCKET* tcp_socket) {
    return queue_is_empty(&tcp_socket->uart_rx_cfg.fifo);
}

void send_tcp_in_ethernet_in_cobs_in_uart(UART_TX_CONFIG* uart_tx_config, device_protocol_TCPSegment* tcp_segment) {
    // Initialize buffer and stream for encapsulating TCPSegment within our EthernetFrame
    uint8_t ethernet_frame_buffer[ETHERNET_FRAME_MAX_SIZE_BYTES];
    pb_ostream_t stream = pb_ostream_from_buffer(ethernet_frame_buffer + ETHERNET_HEADER_SIZE_BYTES, sizeof(ethernet_frame_buffer) - ETHERNET_HEADER_SIZE_BYTES - CRC_SIZE_BYTES);
    
    // Write to stream
    if (!pb_encode(&stream, device_protocol_TCPSegment_fields, tcp_segment)) {
        panic("send_tcp_in_ethernet_in_cobs_in_uart: Failed to encode TCP segment");
        return;
    }

    // Write header
    uint8_t frame_len = ETHERNET_HEADER_SIZE_BYTES + (uint8_t) stream.bytes_written + CRC_SIZE_BYTES;
    if (frame_len > ETHERNET_FRAME_MAX_SIZE_BYTES) {
        panic("send_tcp_in_ethernet_in_cobs_in_uart: Ethernet frame too large");
    } 
    ethernet_frame_buffer[0] = 0;              // version
    ethernet_frame_buffer[1] = frame_len;  // payload length

    // Write crc
    uint16_t crc = crc16(ethernet_frame_buffer, frame_len - CRC_SIZE_BYTES);
    ethernet_frame_buffer[frame_len - 2]     = (uint8_t)(crc & 0xFF);
    ethernet_frame_buffer[frame_len - 1] = (uint8_t)((crc >> 8) & 0xFF);

    // Encode with COBS
    uint8_t cobs_buffer[COBS_MAX_SIZE_BYTES];
    size_t cobs_len = cobs_encode(ethernet_frame_buffer, frame_len, cobs_buffer, sizeof(cobs_buffer));

    // Push over UART
    mutex_enter_blocking(&uart_tx_config->mutex);
    uart_tx_program_putuint8_buf(uart_tx_config->pio, uart_tx_config->sm, cobs_buffer, cobs_len);
    uart_tx_program_putuint8(uart_tx_config->pio, uart_tx_config->sm, 0x00);
    mutex_exit(&uart_tx_config->mutex);

    // Debug
    char output[128];
    size_t offset = 0;
    offset += snprintf(output + offset, sizeof(output) - offset,
                    "send_tcp_in_ethernet_in_cobs_in_uart: %zu, %d, ",
                    cobs_len, crc);
    for (size_t i = 0; i < cobs_len && offset < sizeof(output); i++) {
        offset += snprintf(output + offset, sizeof(output) - offset,
                        "%02X ", cobs_buffer[i]);
    }
    snprintf(output + offset, sizeof(output) - offset,
            "(cobs_len, crc, cobs_data)\n");
    printf("%s", output);
}

bool try_rcv_tcp_from_ethernet_from_cobs_from_uart(UART_RX_CONFIG* uart_rx_config, device_protocol_TCPSegment* tcp_segment) {
    uint pin = uart_rx_config->pin;
    printf("(pin %u) try_rcv_tcp_from_ethernet_from_cobs_from_uart: called\n", pin);
    queue_t* fifo = &uart_rx_config->fifo;
    while(!queue_is_empty(fifo)) {
        // Extract a byte
        uint8_t byte;
        if (!queue_try_remove(fifo, &byte)) {
            panic("(pin %u) try_rcv_tcp_from_ethernet_from_cobs_from_uart: Fifo empty", pin);
        }
        printf("(pin %u) try_rcv_tcp_from_ethernet_from_cobs_from_uart: byte %02X\n", pin, byte);
        
        // Start processing into TCP segment if encounter delimiter and if enough bytes
        if (byte == 0) {
            if (uart_rx_config->cobs_index > 0) {
                uint8_t ethernet_frame_buffer[ETHERNET_FRAME_MAX_SIZE_BYTES];
                size_t decoded_len = cobs_decode(uart_rx_config->cobs_buffer, uart_rx_config->cobs_index, ethernet_frame_buffer, sizeof(ethernet_frame_buffer));
                if (decoded_len > ETHERNET_FRAME_MIN_SIZE_BYTES) {
                    // CRC check
                    uint8_t crc_lo = ethernet_frame_buffer[decoded_len - 2];
                    uint8_t crc_hi = ethernet_frame_buffer[decoded_len - 1];
                    uint16_t received_crc = ((uint16_t)crc_hi << 8) | crc_lo;
                    uint16_t expected_crc = crc16(ethernet_frame_buffer, decoded_len - CRC_SIZE_BYTES);
                    if (received_crc == expected_crc) {
                        // CRC valid
                        pb_istream_t stream = pb_istream_from_buffer(
                            ethernet_frame_buffer + ETHERNET_HEADER_SIZE_BYTES,
                            decoded_len - ETHERNET_HEADER_SIZE_BYTES - CRC_SIZE_BYTES
                        );
                        bool success = pb_decode(&stream, device_protocol_TCPSegment_fields, tcp_segment);

                        // Debug
                        char str[256];
                        int offset = 0;
                        offset += snprintf(str + offset, sizeof(str) - offset,
                                        "(pin %u) try_rcv_tcp_from_ethernet_from_cobs_from_uart: %d, %u, ",
                                        pin, uart_rx_config->cobs_index, received_crc);
                        for (int i = 0; i < uart_rx_config->cobs_index && offset < sizeof(str); i++) {
                            offset += snprintf(str + offset, sizeof(str) - offset,
                                            "%02X ", uart_rx_config->cobs_buffer[i]);
                        }
                        snprintf(str + offset, sizeof(str) - offset,
                                "(cobs_len, crc, cobs_data)\n");
                        printf("%s", str);

                        // Only return on success else continue draining
                        if (success) {
                            return true;
                        } else {
                            printf("(pin %u) try_rcv_tcp_from_ethernet_from_cobs_from_uart: decode failed as %s\n", pin, PB_GET_ERROR(&stream));
                        }
                    } else {
                        // CRC invalid
                        printf("(pin %u) try_rcv_tcp_from_ethernet_from_cobs_from_uart: rcvd CRC %u != %u\n", pin, received_crc, expected_crc);
                    }
                }
            }
            uart_rx_config->cobs_index = 0;
            continue;
        }

        // Check if buffer overflow
        if (uart_rx_config->cobs_index >= COBS_MAX_SIZE_BYTES) {
            // Drain the rest of fifo until new delimiter
            while (!queue_is_empty(fifo)) {
                if (!queue_try_remove(fifo, &byte)) {
                    panic("(pin %u) try_rcv_tcp_from_ethernet_from_cobs_from_uart: Fifo empty while draining due to COBS buffer overflow", pin);
                }
                if (byte == 0) {
                    break;
                }
            }
            printf("(pin %u) try_rcv_tcp_from_ethernet_from_cobs_from_uart: COBS buffer overflow", pin);
            uart_rx_config->cobs_index = 0;
            continue;
        }

        // Put byte into buffer finally
        uart_rx_config->cobs_buffer[uart_rx_config->cobs_index++] = byte; 
    }

    // Reach this point means not able to decode
    return false;
}

void answer_all_tcp(TCP_SOCKET* tcp_socket) {
    printf("answer_all_tcp: Called for pin %u\n", tcp_socket->uart_rx_cfg.pin);
    device_protocol_TCPSegment tcp_segment = device_protocol_TCPSegment_init_zero;
    while(!socket_is_empty(tcp_socket)) {
        bool success = try_rcv_tcp_from_ethernet_from_cobs_from_uart(&tcp_socket->uart_rx_cfg, &tcp_segment);
        if (success) {
            mutex_enter_blocking(&tcp_socket->mutex);
            if (tcp_segment.ack) {
                printf("answer_all_tcp: Received ACK %u\n", tcp_segment.ack_sequence);
                if (is_after_left_and_before_or_eq_right_sliding_window(tcp_socket->tcp_segment_swnd_tail, tcp_segment.ack_sequence, tcp_socket->tcp_segment_swnd_next)) {
                    while (tcp_socket->tcp_segment_swnd_tail != tcp_segment.ack_sequence) {
                        if (tcp_socket->on_rcv[tcp_socket->tcp_segment_swnd_tail]) {
                            tcp_socket->on_rcv[tcp_socket->tcp_segment_swnd_tail](tcp_socket->on_rcv_arg[tcp_socket->tcp_segment_swnd_tail]);
                        }
                        tcp_socket->tcp_segment_swnd_tail = slide_to_right(tcp_socket->tcp_segment_swnd_tail, 1, TCP_SEGMENT_BUFFER_CAPACITY);
                    }
                    printf("answer_all_tcp: Advanced swnd %u, %u, %u (swnd_tail, swnd_next, swnd_head)\n", 
                        tcp_socket->tcp_segment_swnd_tail, tcp_socket->tcp_segment_swnd_next, tcp_socket->tcp_segment_swnd_head);
                }
            } 
            if (tcp_segment.has_application_layer_packet) {
                if (is_at_least_left_and_before_right_sliding_window(tcp_socket->tcp_segment_rwnd_tail, tcp_segment.sequence, tcp_socket->tcp_segment_rwnd_head)) {
                    tcp_socket->tcp_segment_available[tcp_segment.sequence] = true;
                    tcp_socket->tcp_segment_rwnd_buffer[tcp_segment.sequence] = tcp_segment;
                    while (tcp_socket->tcp_segment_available[tcp_socket->tcp_segment_rwnd_tail]) {
                        if (tcp_socket->app_packet_handler) {
                            tcp_socket->app_packet_handler(&tcp_segment.application_layer_packet);
                        }
                        tcp_socket->tcp_segment_available[tcp_socket->tcp_segment_rwnd_tail] = false;
                        tcp_socket->tcp_segment_rwnd_tail = slide_to_right(tcp_socket->tcp_segment_rwnd_tail, 1, TCP_SEGMENT_BUFFER_CAPACITY);
                        tcp_socket->tcp_segment_rwnd_head = slide_to_right(tcp_socket->tcp_segment_rwnd_head, 1, TCP_SEGMENT_BUFFER_CAPACITY);

                        tcp_socket->send_ack = true; // Offload to sender_task or segment_timeout_task to send ACK
                    }
                    printf("answer_all_tcp: Advanced rwnd %u, %u (rwnd_tail, rwnd_head)\n", tcp_socket->tcp_segment_rwnd_tail, tcp_socket->tcp_segment_rwnd_head); 
                } else {
                    printf("answer_all_tcp: Stagnant rwnd %u, %u (rwnd_tail, rwnd_head)\n", tcp_socket->tcp_segment_rwnd_tail, tcp_socket->tcp_segment_rwnd_head); 
                }
                
            } 
            tcp_socket->socket_timeout_timer = TCP_SOCKET_TIMEOUT_TIMER;
            mutex_exit(&tcp_socket->mutex);
        } else {
            printf("answer_all_tcp: Failed to rcv TCP segment\n");
            continue;
        }
    }
};

void segment_timeout_task(void* arg) {
    TCP_SOCKET* tcp_socket = arg;
    mutex_enter_blocking(&tcp_socket->mutex);

    if (tcp_socket->tcp_segment_swnd_tail != tcp_socket->tcp_segment_swnd_next) {
        if (--tcp_socket->segment_timeout_timer <= 0) {
            int tcp_segment_swnd_tail = tcp_socket->tcp_segment_swnd_tail;
            while (tcp_segment_swnd_tail != tcp_socket->tcp_segment_swnd_next) {
                device_protocol_TCPSegment* tcp_segment = &tcp_socket->tcp_segment_swnd_buffer[tcp_segment_swnd_tail];
                if (tcp_socket->send_ack) {
                    tcp_segment->ack = true;
                    tcp_segment->ack_sequence = tcp_socket->tcp_segment_rwnd_tail;
                    printf("segment_timeout_task: Piggybacked ACK %u\n", tcp_socket->tcp_segment_rwnd_tail);
                }
                send_tcp_in_ethernet_in_cobs_in_uart(&tcp_socket->uart_tx_cfg, tcp_segment);
                printf("segment_timeout_task: resent TCP segment %u\n", tcp_segment_swnd_tail);
                tcp_segment_swnd_tail = slide_to_right(tcp_segment_swnd_tail, 1, TCP_SEGMENT_BUFFER_CAPACITY);
            }
            tcp_socket->socket_prevent_timeout_timer = TCP_SOCKET_PREVENT_TIMEOUT_TIMER;
            tcp_socket->segment_timeout_timer = TCP_SEGMENT_TIMEOUT_TIMER;
            printf("segment_timeout_task: timeout %u, %u, %u (tail, next, head)\n", 
                tcp_socket->tcp_segment_swnd_tail, tcp_socket->tcp_segment_swnd_next, tcp_socket->tcp_segment_swnd_head);
        } else {
            printf("segment_timeout_task: %u\n", tcp_socket->segment_timeout_timer);
        }
    } else {
        printf("segment_timeout_task: no pending ACK packets %u, %u, %u (tail, next, head)\n", 
            tcp_socket->tcp_segment_swnd_tail, tcp_socket->tcp_segment_swnd_next, tcp_socket->tcp_segment_swnd_head);
    }

    mutex_exit(&tcp_socket->mutex);
    core1_task_queue_post(segment_timeout_task, tcp_socket);
}

void sender_task(void* arg) {
    TCP_SOCKET* tcp_socket = arg;
    mutex_enter_blocking(&tcp_socket->mutex);

    if (tcp_socket->tcp_segment_swnd_next != tcp_socket->tcp_segment_swnd_head) {
        // Unsent segments present in queue
        bool sent = false;
        while (left_to_right_distance_sliding_window(tcp_socket->tcp_segment_swnd_tail, tcp_socket->tcp_segment_swnd_next, TCP_SEGMENT_BUFFER_CAPACITY) < tcp_socket->cwnd) {
            // Not congested
            if (tcp_socket->send_ack) {
                tcp_socket->tcp_segment_swnd_buffer[tcp_socket->tcp_segment_swnd_next].ack = true;
                tcp_socket->tcp_segment_swnd_buffer[tcp_socket->tcp_segment_swnd_next].ack_sequence = tcp_socket->tcp_segment_rwnd_tail;
                printf("sender_task: Piggybacked ACK %u\n", tcp_socket->tcp_segment_rwnd_tail);
            }
            send_tcp_in_ethernet_in_cobs_in_uart(&tcp_socket->uart_tx_cfg, &tcp_socket->tcp_segment_swnd_buffer[tcp_socket->tcp_segment_swnd_next]);
            sent = true;
            tcp_socket->tcp_segment_swnd_next = slide_to_right(tcp_socket->tcp_segment_swnd_next, 1, TCP_SEGMENT_BUFFER_CAPACITY);
        }
        if (sent) {
            printf("sender_task: Sent %u, %u, %u (tail, next, head)\n", 
                tcp_socket->tcp_segment_swnd_tail, tcp_socket->tcp_segment_swnd_next, tcp_socket->tcp_segment_swnd_head);
            tcp_socket->socket_prevent_timeout_timer = TCP_SOCKET_PREVENT_TIMEOUT_TIMER;
            tcp_socket->segment_timeout_timer = TCP_SEGMENT_TIMEOUT_TIMER;
        } else {
            printf("sender_task: Congested %u, %u, %u (tail, next, head)\n", 
                tcp_socket->tcp_segment_swnd_tail, tcp_socket->tcp_segment_swnd_next, tcp_socket->tcp_segment_swnd_head);
        }
    } else {
        printf("sender_task: No segments to send %u, %u, %u (tail, next, head)\n", 
            tcp_socket->tcp_segment_swnd_tail, tcp_socket->tcp_segment_swnd_next, tcp_socket->tcp_segment_swnd_head);
    }

    mutex_exit(&tcp_socket->mutex);
    core1_task_queue_post(sender_task, tcp_socket);
}

void socket_timeout_task(void* arg) {
    TCP_SOCKET* tcp_socket = arg;
    mutex_enter_blocking(&tcp_socket->mutex);

    if (tcp_socket->socket_timeout_timer > 0) {
        if (--tcp_socket->socket_timeout_timer <= 0) {
            printf("socket_timeout_task: socket timeout\n");
            if (tcp_socket->socket_timeout_handler) {
                tcp_socket->socket_timeout_handler(tcp_socket);
            }
        }
    }

    if (--tcp_socket->socket_prevent_timeout_timer <= 0) {
        printf("socket_timeout_task: preventing socket timeout on other end\n");
        device_protocol_ApplicationLayerPacket app_layer_packet = device_protocol_ApplicationLayerPacket_init_zero;
        app_layer_packet.component = device_protocol_ApplicationLayerPacket_Component_BULB;
        async_send_app_packet_in_tcp(tcp_socket, &app_layer_packet, NULL, NULL, NULL, NULL);
        tcp_socket->socket_prevent_timeout_timer = TCP_SOCKET_PREVENT_TIMEOUT_TIMER;
    }

    mutex_exit(&tcp_socket->mutex);
    core1_task_queue_post(socket_timeout_task, tcp_socket);
}

TCP_SOCKET* init_socket(TCP_SOCKET_CFG* tcp_socket_cfg) {
    if (sockets_initialized >= MAX_TCP_SOCKETS) 
        panic("init_socket: Cannot support more than %u sockets on this hardware", MAX_TCP_SOCKETS);

    TCP_SOCKET* tcp_socket = &tcp_sockets[sockets_initialized];
    tcp_socket->cwnd = CWND;
    if (tcp_socket->cwnd <= 0) {
        panic("init_socket: Invalid cwnd value");
    }
    tcp_socket->tcp_segment_rwnd_head = slide_to_right(tcp_socket->tcp_segment_rwnd_head, tcp_socket->cwnd, TCP_SEGMENT_BUFFER_CAPACITY);

    // Initialize RX
    tcp_socket->uart_rx_cfg.pin = tcp_socket_cfg->rx_pin;
    tcp_socket->uart_rx_cfg.pio_irq_func_wrapper = pio_irq_func_wrappers[sockets_initialized];
    tcp_socket->uart_rx_cfg.worker.do_work = tcp_socket_cfg->answer_all_tcp_wrapper;
    uart_rx_pio_on_gpio(&tcp_socket->uart_rx_cfg);

    // Initialize TX
    tcp_socket->uart_tx_cfg.pin = tcp_socket_cfg->tx_pin;
    uart_tx_pio_on_gpio(&tcp_socket->uart_tx_cfg);

    // Initialize mutex
    mutex_init(&tcp_socket->mutex);

    // Initialize socket timeout
    tcp_socket->socket_timeout_timer = TCP_SOCKET_TIMEOUT_TIMER;
    tcp_socket->socket_prevent_timeout_timer = TCP_SOCKET_PREVENT_TIMEOUT_TIMER;

    // Initialize tasks
    core1_task_queue_post(segment_timeout_task, tcp_socket);
    core1_task_queue_post(sender_task, tcp_socket);
    core1_task_queue_post(socket_timeout_task, tcp_socket);

    sockets_initialized++;
    
    return tcp_socket;
}

bool async_send_app_packet_in_tcp(TCP_SOCKET* tcp_socket, device_protocol_ApplicationLayerPacket* application_layer_packet, 
    core1_task_fn_t on_rcv, void* on_rcv_arg, core1_task_fn_t on_timeout, void* on_timeout_arg) 
{
    mutex_enter_blocking(&tcp_socket->mutex);

    // Check if queue is empty
    uint8_t next_head = slide_to_right(tcp_socket->tcp_segment_swnd_head, 1, TCP_SEGMENT_BUFFER_CAPACITY);
    if (next_head == tcp_socket->tcp_segment_swnd_tail) {
        printf("async_send_app_packet_in_tcp: Queue full; %u, %u, %u (tail, next, head)\n", tcp_socket->tcp_segment_swnd_tail, tcp_socket->tcp_segment_swnd_next, tcp_socket->tcp_segment_swnd_head);
        mutex_exit(&tcp_socket->mutex);
        return false;
    }
    
    // Create segment and put it in queue
    device_protocol_TCPSegment* segment = &tcp_socket->tcp_segment_swnd_buffer[tcp_socket->tcp_segment_swnd_head];
    segment->application_layer_packet = *application_layer_packet;
    segment->has_application_layer_packet = true;
    segment->sequence = tcp_socket->tcp_segment_swnd_head;
    segment->ack = false;

    // Increment socket head
    tcp_socket->tcp_segment_swnd_head = next_head;

    mutex_exit(&tcp_socket->mutex);
    printf("async_send_app_packet_in_tcp: %u, %u, %u (tail, next, head)\n", tcp_socket->tcp_segment_swnd_tail, tcp_socket->tcp_segment_swnd_next, tcp_socket->tcp_segment_swnd_head);
    return true;
}