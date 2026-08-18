target_sources(RP2350-Touch-LCD-2 PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/neighbor_table.c
    ${CMAKE_CURRENT_LIST_DIR}/network.c
)

target_include_directories(RP2350-Touch-LCD-2 PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
