addtest(crdt_test
    crdt_hierarchical_key_test.cpp
    crdt_set_test.cpp
    crdt_heads_test.cpp
    crdt_datastore_test.cpp
    crdt_atomic_transaction_test.cpp
    crdt_custom_broadcaster.cpp
    crdt_mirror_broadcaster.cpp
    crdt_custom_dagsyncer.cpp
)

include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(crdt_test
    crdt_datastore
    crdt_globaldb_proto
    rocksdb
    database_error
    ipfs-lite-cpp::cid
    ipfs-lite-cpp::ipfs_datastore_in_memory
    ipfs-lite-cpp::blake2
    p2p::p2p_cid
    p2p::p2p_multihash
    p2p::p2p_multiaddress
    Boost::headers
    Boost::filesystem
    crdt_heads
)

if(FORCE_MULTIPLE)
  set_target_properties(crdt_test PROPERTIES LINK_FLAGS "${MULTIPLE_OPTION}")
endif()

addtest(globaldb_integration_test
    globaldb_integration.cpp
)


target_link_libraries(globaldb_integration_test
    crdt_globaldb
)

if(FORCE_MULTIPLE)
  set_target_properties(crdt_test PROPERTIES LINK_FLAGS "${MULTIPLE_OPTION}")
endif()

