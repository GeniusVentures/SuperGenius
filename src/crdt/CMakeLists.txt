#add_subdirectory(proto)

set(PROTO_SRC_DIR ${CMAKE_CURRENT_BINARY_DIR}/proto)
file(MAKE_DIRECTORY ${PROTO_SRC_DIR})

set(protobuf_generate_PROTOC_OUT_DIR ${PROTO_SRC_DIR})

add_library(hierarchical_key 
	hierarchical_key.hpp
	impl/hierarchical_key.cpp
	)
target_link_libraries(hierarchical_key
    Boost::boost
    )
supergenius_install(hierarchical_key)


add_library(crdt_set 
	crdt_set.hpp
	impl/crdt_set.cpp
    )
target_include_directories(crdt_set PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(crdt_set
    Boost::boost
	buffer
	rocksdb
	hierarchical_key
	protobuf::libprotobuf
    )
protobuf_generate(
	LANGUAGE cpp
	TARGET crdt_set
	PROTOS proto/delta.proto)
supergenius_install(crdt_set)


add_library(crdt_heads 
	crdt_heads.hpp
	impl/crdt_heads.cpp
    )
target_link_libraries(crdt_heads
    Boost::boost
	buffer
	hierarchical_key
	rocksdb
    )


add_library(crdt_datastore 
	crdt_options.hpp 
    crdt_datastore.hpp
	impl/crdt_datastore.cpp
    )
target_include_directories(crdt_datastore PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(crdt_datastore
    Boost::boost
	buffer
    ipfs-lite-cpp::buffer
    ipfs-lite-cpp::ipld_node
	ipfs_merkledag_service
	crdt_set
	crdt_heads
    )
protobuf_generate(
	LANGUAGE cpp
	TARGET crdt_datastore
	PROTOS proto/bcast.proto)
supergenius_install(crdt_datastore)
