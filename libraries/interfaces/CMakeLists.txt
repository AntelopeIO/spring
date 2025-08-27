set(PROTOBUF_PROTOC ${CMAKE_BINARY_DIR}/libraries/protobuf/protoc)

set(PROTO_PATH "${CMAKE_CURRENT_SOURCE_DIR}/proto")
set(OUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/generated")
file(MAKE_DIRECTORY ${OUT_PATH})

set(PROTOC_ARGS
    --cpp_out "${OUT_PATH}"
    -I "${PROTO_PATH}"
)

set(PROTO_FILES
    ${PROTO_PATH}/types/types.proto
    ${PROTO_PATH}/apiv2/error_results.proto
    ${PROTO_PATH}/apiv2/get_raw_code_and_abi.proto
    ${PROTO_PATH}/apiv2/get_info.proto
)

set(GENERATED_SOURCES)
set(GENERATED_HEADERS)
foreach(PROTO_FILE ${PROTO_FILES})
    file(RELATIVE_PATH PROTO_REL_PATH ${PROTO_PATH} ${PROTO_FILE})
    get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
    get_filename_component(PROTO_REL_DIR ${PROTO_REL_PATH} DIRECTORY)
    set(SOURCE_FILE "${OUT_PATH}/${PROTO_REL_DIR}/${PROTO_NAME}.pb.cc")
    set(HEADER_FILE "${OUT_PATH}/${PROTO_REL_DIR}/${PROTO_NAME}.pb.h")
    list(APPEND GENERATED_SOURCES "${SOURCE_FILE}")
    list(APPEND GENERATED_HEADERS "${HEADER_FILE}")
    file(MAKE_DIRECTORY "${OUT_PATH}/${PROTO_REL_DIR}")
endforeach()

# Create a list of relative paths for the COMMENT
set(PROTO_FILES_RELATIVE)
foreach(PROTO_FILE ${PROTO_FILES})
    file(RELATIVE_PATH REL_PATH ${CMAKE_CURRENT_SOURCE_DIR} ${PROTO_FILE})
    list(APPEND PROTO_FILES_RELATIVE ${REL_PATH})
endforeach()
string(REPLACE ";" " " PROTO_FILES_RELATIVE_STR "${PROTO_FILES_RELATIVE}")

add_custom_command(
    OUTPUT ${GENERATED_SOURCES} ${GENERATED_HEADERS}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${OUT_PATH}"
    COMMAND ${PROTOBUF_PROTOC} ${PROTOC_ARGS} ${PROTO_FILES}
    DEPENDS ${PROTO_FILES} protoc
    COMMENT "Running protoc C++ compiler on ${PROTO_FILES_RELATIVE_STR}"
    VERBATIM
)

add_library(interfaces STATIC
    ${GENERATED_SOURCES}
    ${GENERATED_HEADERS}
)

target_include_directories(interfaces PUBLIC ${OUT_PATH})

target_link_libraries(interfaces PUBLIC protobuf::libprotobuf)

add_dependencies(interfaces protoc)

add_custom_target(generate_protos DEPENDS ${GENERATED_SOURCES} ${GENERATED_HEADERS})
add_dependencies(interfaces generate_protos)