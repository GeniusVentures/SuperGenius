add_library(hierarchical_key
    impl/hierarchical_key.cpp
)
target_link_libraries(hierarchical_key
    PRIVATE
    Boost::headers
)
supergenius_install(hierarchical_key)

add_proto_library(crdt_delta proto/delta.proto)
add_proto_library(crdt_bcast proto/bcast.proto)
add_proto_library(crdt_proto_heads proto/heads.proto)
add_library(crdt_set
    impl/crdt_set.cpp
)
target_link_libraries(crdt_set
    PUBLIC
    Boost::headers
    rocksdb
    hierarchical_key
    crdt_delta
)
supergenius_install(crdt_set)

add_library(crdt_heads
    impl/crdt_heads.cpp
)
target_link_libraries(crdt_heads
    PUBLIC
    hierarchical_key
    rocksdb
    ipfs-lite-cpp::cid
    crdt_proto_heads
)
supergenius_install(crdt_heads)

add_library(crdt_graphsync_dagsyncer
    dagsyncer.hpp
    graphsync_dagsyncer.hpp
    impl/graphsync_dagsyncer.cpp
)
target_link_libraries(crdt_graphsync_dagsyncer
    PUBLIC
    ipfs-lite-cpp::ipfs_merkledag_service
    ipfs-lite-cpp::graphsync
    logger
)

supergenius_install(crdt_graphsync_dagsyncer)

add_library(crdt_data_filter
    impl/crdt_data_filter.cpp
)
target_link_libraries(crdt_data_filter
    PUBLIC
    crdt_delta
)

supergenius_install(crdt_data_filter)

add_library(crdt_datastore
    impl/crdt_datastore.cpp
    impl/atomic_transaction.cpp
)
target_link_libraries(crdt_datastore
    PUBLIC
    Boost::headers
    crdt_set
    crdt_heads
    crdt_graphsync_dagsyncer
    crdt_data_filter
    logger
    PRIVATE
    crdt_bcast
    crdt_proto_heads
)
supergenius_install(crdt_datastore)

add_subdirectory(globaldb)
