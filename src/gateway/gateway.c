#include "gateway.h"

#include "network.h"
#include "pb_encode.h"
#include "pico/stdlib.h"
#include "protocol.pb.h"
#include "web_usb.h"

#include <string.h>


static bool database_scan_active = false;
static size_t next_database_index = 0;

static void send_packet(const NetworkPacket *packet) {
    uint8_t encoded_packet[NetworkPacket_size];
    pb_ostream_t stream = pb_ostream_from_buffer(
        encoded_packet,
        sizeof(encoded_packet)
    );

    bool encoded = pb_encode(&stream, &NetworkPacket_msg, packet);
    hard_assert(encoded);
    hard_assert(web_usb_send(encoded_packet, stream.bytes_written));
}

static void send_gateway_hello(const NodeIdentity *identity) {
    NetworkPacket packet = NetworkPacket_init_zero;
    memcpy(
        packet.source_node_id,
        identity->node_id.id,
        sizeof(packet.source_node_id)
    );
    packet.boot_id = identity->boot_id;
    packet.which_payload = NetworkPacket_gateway_hello_tag;

    send_packet(&packet);
}

static void send_next_database_packet(void) {
    if (!database_scan_active || !web_usb_can_send()) {
        return;
    }

    while (next_database_index < NETWORK_LINK_STATE_DATABASE_CAPACITY) {
        NetworkPacket packet;
        size_t entry_index = next_database_index;
        next_database_index++;

        if (!network_get_link_state_database_packet(entry_index, &packet)) {
            continue;
        }

        hard_assert(packet.which_payload == NetworkPacket_link_state_tag);
        send_packet(&packet);
        return;
    }

    database_scan_active = false;
}

static void send_next_database_update(void) {
    if (!web_usb_can_send()) {
        return;
    }

    size_t entry_index;
    if (!network_take_link_state_database_update(&entry_index)) {
        return;
    }

    NetworkPacket packet;
    bool entry_exists = network_get_link_state_database_packet(
        entry_index,
        &packet
    );
    hard_assert(entry_exists);
    hard_assert(packet.which_payload == NetworkPacket_link_state_tag);

    send_packet(&packet);
}

void gateway_update(const NodeIdentity *identity) {
    hard_assert(identity != NULL);

    bool connected;
    if (web_usb_take_connection_change(&connected)) {
        network_set_gateway_connected(connected);

        if (!connected) {
            database_scan_active = false;
            next_database_index = 0;
            return;
        }

        network_clear_link_state_database_updates();
        send_gateway_hello(identity);
        database_scan_active = true;
        next_database_index = 0;
        return;
    }

    if (database_scan_active) {
        send_next_database_packet();
        return;
    }

    send_next_database_update();
}
