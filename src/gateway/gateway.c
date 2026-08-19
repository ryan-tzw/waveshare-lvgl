#include "gateway.h"

#include "network.h"
#include "pb_encode.h"
#include "pico/stdlib.h"
#include "protocol.pb.h"
#include "web_usb.h"

#include <string.h>


static bool initial_database_transfer_active = false;
static size_t next_initial_database_entry_index = 0;

static void encode_and_queue_network_packet(const NetworkPacket *packet) {
    uint8_t encoded_packet[NetworkPacket_size];
    pb_ostream_t stream = pb_ostream_from_buffer(encoded_packet, sizeof(encoded_packet));

    hard_assert( pb_encode(&stream, &NetworkPacket_msg, packet) );
    hard_assert( web_usb_send(encoded_packet, stream.bytes_written) );
}

static void send_gateway_hello(const NodeIdentity *identity) {
    NetworkPacket packet = NetworkPacket_init_zero;
    memcpy(packet.source_node_id, identity->node_id.id, sizeof(packet.source_node_id));
    packet.boot_id       = identity->boot_id;
    packet.which_payload = NetworkPacket_gateway_hello_tag;

    encode_and_queue_network_packet(&packet);
}

/*
 * Send the initial database contents after WebUSB connects. Scan entries in index order,
 * skip empty slots, and enqueue one LINK_STATE per call.
 */
static void send_next_initial_link_state_if_possible(void) {
    if (!initial_database_transfer_active || !web_usb_can_send()) { return; }

    while (next_initial_database_entry_index < NETWORK_LINK_STATE_DATABASE_CAPACITY) {
        NetworkPacket packet;
        size_t database_entry_index = next_initial_database_entry_index;
        next_initial_database_entry_index++;

        if (!network_get_link_state_database_packet(database_entry_index, &packet)) { continue; }

        hard_assert(packet.which_payload == NetworkPacket_link_state_tag);
        encode_and_queue_network_packet(&packet);
        return;
    }

    initial_database_transfer_active = false;
}

/*
 * After the initial database transfer, consume one pending database-change
 * notification and queue that entry's latest LINK_STATE whenever WebUSB is
 * ready. Changes are coalesced independently for each database entry.
 */
static void send_next_changed_link_state_if_possible(void) {
    if (!web_usb_can_send()) { return; }

    size_t database_entry_index;
    if (!network_take_link_state_database_update(&database_entry_index)) { return; }

    NetworkPacket packet;
    hard_assert( network_get_link_state_database_packet(database_entry_index, &packet) );
    hard_assert( packet.which_payload == NetworkPacket_link_state_tag );

    encode_and_queue_network_packet(&packet);
}

void gateway_update(const NodeIdentity *identity) {
    hard_assert(identity != NULL);

    bool web_usb_connected;
    if (web_usb_take_connection_change(&web_usb_connected)) {
        network_set_gateway_connected(web_usb_connected);

        if (!web_usb_connected) {
            initial_database_transfer_active = false;
            next_initial_database_entry_index  = 0;
            return;
        }

        network_clear_link_state_database_updates();
        send_gateway_hello(identity);
        initial_database_transfer_active = true;
        next_initial_database_entry_index  = 0;
        return;
    }

    if (initial_database_transfer_active) {
        send_next_initial_link_state_if_possible();
        return;
    }

    send_next_changed_link_state_if_possible();
}
