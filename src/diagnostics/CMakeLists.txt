target_sources(RP2350-Touch-LCD-2 PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/debug_diagnostics.c
    ${CMAKE_CURRENT_LIST_DIR}/timing_diagnostics.c
    ${CMAKE_CURRENT_LIST_DIR}/transport_diagnostics.c
)

target_include_directories(RP2350-Touch-LCD-2 PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
