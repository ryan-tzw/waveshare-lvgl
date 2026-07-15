add_library(socket)
target_sources(socket PRIVATE socket.c)
target_link_libraries(socket
        pio
        pico_stdlib
        proto_generated
        pico_multicore
        hardware_pio
        core1_task_queue
        cobs
)
target_include_directories(socket PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})