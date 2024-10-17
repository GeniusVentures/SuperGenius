
add_library(hierarchical_key
	impl/hierarchical_key.cpp
	)
target_link_libraries(hierarchical_key
    Boost::boost
    )
set_target_properties(hierarchical_key PROPERTIES PUBLIC_HEADER "hierarchical_key.hpp")
supergenius_install(hierarchical_key)

add_proto_library(crdt_delta proto/delta.proto)
add_proto_library(crdt_bcast proto/bcast.proto)
add_library(crdt_set 
    impl/crdt_set.cpp
    )
set_target_properties(crdt_set PROPERTIES PUBLIC_HEADER "crdt_set.hpp")
target_link_libraries(crdt_set
    Boost::boost
    buffer
    rocksdb
    hierarchical_key
    crdt_delta
    )
supergenius_install(crdt_set)

add_library(crdt_heads 
    impl/crdt_heads.cpp
    )
set_target_properties(crdt_heads PROPERTIES PUBLIC_HEADER "crdt_heads.hpp")
target_link_libraries(crdt_heads
    Boost::boost
    buffer
    hierarchical_key
    rocksdb
    ipfs-lite-cpp::cid
)
supergenius_install(crdt_heads)

add_library(crdt_datastore 
    impl/crdt_datastore.cpp
    )
set_target_properties(crdt_datastore PROPERTIES PUBLIC_HEADER "crdt_options.hpp;crdt_datastore.hpp")
target_link_libraries(crdt_datastore
    Boost::boost
    buffer
    ipfs-lite-cpp::buffer
    ipfs-lite-cpp::ipld_node
    ipfs-lite-cpp::ipfs_merkledag_service
    crdt_set
    crdt_heads
    crdt_bcast
    )
supergenius_install(crdt_datastore)

add_library(crdt_graphsync_dagsyncer 
    dagsyncer.hpp 
    graphsync_dagsyncer.hpp 
    impl/graphsync_dagsyncer.cpp
    )
set_target_properties(crdt_graphsync_dagsyncer PROPERTIES PUBLIC_HEADER "dagsyncer.hpp;graphsync_dagsyncer.hpp")
target_link_libraries(crdt_graphsync_dagsyncer
    Boost::boost
    ipfs-lite-cpp::graphsync
    ipfs-lite-cpp::buffer
    ipfs-lite-cpp::ipld_node
    ipfs-lite-cpp::ipfs_merkledag_service
    )
supergenius_install(crdt_graphsync_dagsyncer)

add_subdirectory(globaldb)
