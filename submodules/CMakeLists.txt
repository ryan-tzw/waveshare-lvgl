add_subdirectory(nanopb)

add_library(cobs STATIC cobs-c/cobs.c)
target_include_directories(cobs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cobs-c)

add_library(libcrc STATIC libcrc/src/crc16.c)
target_include_directories(libcrc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/libcrc/include)