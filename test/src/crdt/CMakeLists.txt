addtest(crdt_test
    crdt_hierarchical_key_test.cpp
    crdt_set_test.cpp
    crdt_heads_test.cpp
    crdt_datastore_test.cpp
)
target_link_libraries(crdt_test
    crdt_datastore
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
