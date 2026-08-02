target_sources(RP2350-Touch-LCD-2 PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/node_identity.c
)

target_include_directories(RP2350-Touch-LCD-2 PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(RP2350-Touch-LCD-2
    hardware_flash
    pico_unique_id
)
