add_library(cobs)
target_sources(cobs PRIVATE cobs.c)
target_link_libraries(cobs

)
target_include_directories(cobs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})