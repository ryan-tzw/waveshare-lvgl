add_library(simulator_engine)
target_sources(simulator_engine PRIVATE simulator_engine.c)
target_link_libraries(simulator_engine PUBLIC
        socket
)
target_link_libraries(simulator_engine PRIVATE
        pio
        proto_generated
        core1_task_queue
        logging
)
target_include_directories(simulator_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})