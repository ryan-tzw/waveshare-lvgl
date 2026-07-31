nanopb_generate_cpp(
    TARGET protocol_messages
    protocol.proto
)

target_link_libraries(
    RP2350-Touch-LCD-2
    protocol_messages
)
