add_library(logging)
target_compile_definitions(logging PUBLIC
    LOG_LEVEL=LOG_LEVEL_INFO
)
target_sources(logging PRIVATE logging.c)
target_link_libraries(logging PRIVATE
    pico_multicore
    pico_stdlib
)
target_include_directories(logging PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})