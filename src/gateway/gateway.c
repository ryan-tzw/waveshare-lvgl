#include "gateway.h"

#include "pb_encode.h"
#include "pico/stdlib.h"
#include "protocol.pb.h"
#include "web_usb.h"

#include <string.h>


static void send_gateway_hello(const NodeIdentity *identity) {
    NetworkPacket packet = NetworkPacket_init_zero;
    memcpy(
        packet.source_node_id,
        identity->node_id.id,
        sizeof(packet.source_node_id)
    );
    packet.boot_id = identity->boot_id;
    packet.which_payload = NetworkPacket_gateway_hello_tag;

    uint8_t encoded_packet[NetworkPacket_size];
    pb_ostream_t stream = pb_ostream_from_buffer(
        encoded_packet,
        sizeof(encoded_packet)
    );

    bool encoded = pb_encode(&stream, &NetworkPacket_msg, &packet);
    hard_assert(encoded);
    hard_assert(web_usb_send(encoded_packet, stream.bytes_written));
}

void gateway_update(const NodeIdentity *identity) {
    hard_assert(identity != NULL);

    if (web_usb_take_connected_event()) {
        send_gateway_hello(identity);
    }
}
