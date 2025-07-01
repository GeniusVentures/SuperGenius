set(PROTOS
    delta.proto
    bcast.proto
)

set(PROTO_SRC_DIR ${CMAKE_CURRENT_BINARY_DIR}/proto_buf)
file(MAKE_DIRECTORY ${PROTO_SRC_DIR})
include_directories(${PROTO_SRC_DIR})

set(Protobuf_USE_STATIC_LIBS ON)
protobuf_generate_cpp(DELTA_PROTO_SRCS DELTA_PROTO_HDRS ${CMAKE_CURRENT_LIST_DIR} PROTO_PATH delta.proto)

protobuf_generate_cpp(BCAST_PROTO_SRCS BCAST_PROTO_HDRS ${CMAKE_CURRENT_LIST_DIR} PROTO_PATH bcast.proto)
