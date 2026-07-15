add_library(crc16)
target_sources(crc16 PRIVATE crc16.c)
target_link_libraries(crc16
)
target_include_directories(crc16 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})