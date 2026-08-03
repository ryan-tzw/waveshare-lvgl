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

set(BOOT_ID_JOURNAL_SIZE_BYTES 4096)
set(
    BOOT_ID_JOURNAL_LINKER_SCRIPT
    ${CMAKE_CURRENT_BINARY_DIR}/boot_id_journal_guard.ld
)

configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/boot_id_journal_guard.ld.in
    ${BOOT_ID_JOURNAL_LINKER_SCRIPT}
    @ONLY
)

target_link_options(RP2350-Touch-LCD-2 PRIVATE
    "LINKER:--script=${BOOT_ID_JOURNAL_LINKER_SCRIPT}"
)

pico_add_link_depend(
    RP2350-Touch-LCD-2
    ${BOOT_ID_JOURNAL_LINKER_SCRIPT}
)
