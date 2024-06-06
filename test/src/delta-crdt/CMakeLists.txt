
addtest(delta-crdt_test
    crdt_set_test.cpp
    )
target_link_libraries(delta-crdt_test
	delta-enabled-crdts
    crdt_datastore
    rocksdb
    database_error
    cid
    ipfs-lite-cpp::ipfs_datastore_in_memory
    ipfs-lite-cpp::blake2
    p2p::p2p_cid
    p2p::p2p_multihash
    p2p::p2p_multiaddress
    Boost::boost
    Boost::filesystem
    )
if(FORCE_MULTILE)
  set_target_properties(crdt_test PROPERTIES LINK_FLAGS "${MULTIPLE_OPTION}")
endif()
