
addtest(crdt_test
    crdt_hierarchical_key_test.cpp
	crdt_set_test.cpp
	crdt_heads_test.cpp 
    )
target_link_libraries(crdt_test
    crdt_set
	crdt_heads
	hierarchical_key
	leveldb
	database_error
	cid
	ipfs-lite-cpp::blake2
	p2p::p2p_cid
	p2p::p2p_multihash
	p2p::p2p_multiaddress
	Boost::boost
	Boost::filesystem
    )
set_target_properties(crdt_test PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE")

