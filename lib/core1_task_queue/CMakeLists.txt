add_library(core1_task_queue)
target_sources(core1_task_queue PRIVATE core1_task_queue.c)
target_link_libraries(core1_task_queue PRIVATE
        pico_multicore
        pico_stdlib
        logging
)
target_include_directories(core1_task_queue PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})