# target_sources(RP2350-Touch-LCD-2 PRIVATE ${CMAKE_CURRENT_LIST_DIR}/pio_uart.c)

pico_generate_pio_header(RP2350-Touch-LCD-2 ${CMAKE_CURRENT_LIST_DIR}/uart_tx.pio)
pico_generate_pio_header(RP2350-Touch-LCD-2 ${CMAKE_CURRENT_LIST_DIR}/uart_rx.pio)

target_include_directories(RP2350-Touch-LCD-2 PRIVATE ${CMAKE_CURRENT_LIST_DIR})