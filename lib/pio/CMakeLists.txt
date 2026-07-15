# add_executable(pio_uart_rx)

# pico_generate_pio_header(pio_uart_rx ${CMAKE_CURRENT_LIST_DIR}/uart_rx.pio)

# target_sources(pio_uart_rx PRIVATE uart_rx.c)

# target_link_libraries(pio_uart_rx PRIVATE
#         pico_stdlib
#         pico_multicore
#         hardware_pio
#         )

# pico_add_extra_outputs(pio_uart_rx)

# # add url via pico_set_program_url
# # example_auto_set_url(pio_uart_rx)

# Similar to above but uses an interrupt for RX
# add_executable(pio_uart_rx_intr)
# pico_generate_pio_header(pio_uart_rx_intr ${CMAKE_CURRENT_LIST_DIR}/uart_rx.pio)
# target_sources(pio_uart_rx_intr PRIVATE uart_rx_intr.c)
# target_link_libraries(pio_uart_rx_intr PRIVATE
#         pico_stdlib
#         pico_multicore
#         hardware_pio
#         pico_async_context_threadsafe_background
#         )
# pico_add_extra_outputs(pio_uart_rx_intr)
# example_auto_set_url(pio_uart_rx_intr)

add_library(pio)
pico_generate_pio_header(pio ${CMAKE_CURRENT_LIST_DIR}/uart_rx.pio)
pico_generate_pio_header(pio ${CMAKE_CURRENT_LIST_DIR}/uart_tx.pio)
target_sources(pio PRIVATE pio.c)
target_link_libraries(pio
        pico_stdlib
        pico_multicore
        hardware_pio
        hardware_dma
        pico_async_context_threadsafe_background
        proto_generated
        crc16
)
target_include_directories(pio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})