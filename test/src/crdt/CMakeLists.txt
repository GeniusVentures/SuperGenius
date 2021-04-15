
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
	cid
    ipfs-lite-cpp::ipfs_datastore_in_memory
	ipfs-lite-cpp::blake2
	p2p::p2p_cid
	p2p::p2p_multihash
	p2p::p2p_multiaddress
	Boost::boost
	Boost::filesystem
    )
set_target_properties(crdt_test PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE")


add_executable(globaldb globaldb.cpp)
target_link_libraries(globaldb 
    crdt_datastore
    ipfs-lite-cpp::ipfs_datastore_rocksdb
    p2p::p2p_peer_address
    p2p::p2p_multiaddress
    Boost::boost
    Boost::filesystem
    Boost::program_options
    )
set_target_properties(globaldb PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE")
