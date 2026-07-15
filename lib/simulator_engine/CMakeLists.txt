add_library(simulator_engine)
target_sources(simulator_engine PRIVATE simulator_engine.c)
target_link_libraries(simulator_engine
        pio
        proto_generated
        crc16
        core1_task_queue
        socket
)
target_include_directories(simulator_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})