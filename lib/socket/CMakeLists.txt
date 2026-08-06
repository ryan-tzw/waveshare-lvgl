add_library(socket)
target_sources(socket PRIVATE socket.c)
target_link_libraries(socket PRIVATE
        core1_task_queue
        cobs
        logging
        crc16
)
target_link_libraries(socket PUBLIC
        pio
        proto_generated
)
target_include_directories(socket PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})