find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(PROTO_FILE
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol.proto
)
set(NANOPB_GENERATOR
    ${CMAKE_SOURCE_DIR}/lib/nanopb/generator/nanopb_generator.py
)
set(GENERATED_C
    ${CMAKE_CURRENT_BINARY_DIR}/protocol.pb.c
)
set(GENERATED_H
    ${CMAKE_CURRENT_BINARY_DIR}/protocol.pb.h
)

add_custom_command(
    OUTPUT
        ${GENERATED_C}
        ${GENERATED_H}
    COMMAND
        ${Python3_EXECUTABLE}
        ${NANOPB_GENERATOR}
        -I${CMAKE_CURRENT_SOURCE_DIR}
        --output-dir=${CMAKE_CURRENT_BINARY_DIR}
        protocol.proto
    DEPENDS
        ${PROTO_FILE}
        ${NANOPB_GENERATOR}
    WORKING_DIRECTORY
        ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT
        "Generating nanopb files from protocol.proto"
)

add_library(proto_generated
    ${GENERATED_C}
    ${GENERATED_H}
)

target_include_directories(proto_generated PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(proto_generated PUBLIC
    protobuf-nanopb-static
)