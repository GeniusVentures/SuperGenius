
set(PROTOS
    delta.proto 
	bcast.proto 
)

set(PROTO_SRC_DIR ${CMAKE_CURRENT_BINARY_DIR}/proto-src)
file(MAKE_DIRECTORY ${PROTO_SRC_DIR})
include_directories(${PROTO_SRC_DIR})

set(Protobuf_USE_STATIC_LIBS ON)
protobuf_generate_cpp(DELTA_PROTO_SRCS DELTA_PROTO_HDRS ${PROTO_SRC_DIR} delta.proto)

protobuf_generate_cpp(BCAST_PROTO_SRCS BCAST_PROTO_HDRS ${PROTO_SRC_DIR} bcast.proto)