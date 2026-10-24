add_proto_library(crdt_globaldb_proto proto/broadcast.proto)

# Cold-storage stack (SPILL-01, v1.1 Phase 8): the stub is ALWAYS compiled
# so the factory/lifecycle-bridge symbols exist in default OFF builds with
# zero sgns-blockstore linkage; the real stack + the journal-spill decorator
# compile (and sgns-blockstore links) ONLY under SGNS_COLD_STORAGE (the
# option + find_package chain shipped with Phase 7 - not duplicated here).
# The real TU and the stub define the SAME symbols by design (gated
# factory/stub split) - they must NEVER land in the same unity blob.
set_source_files_properties(ColdStorageStack.cpp ColdStorageStackStub.cpp
    ColdStorageManager.cpp BlockStoreBackedIpfsDatastore.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)

add_library(crdt_globaldb
    GlobalDbNetworkComposition.cpp
    globaldb.cpp
    pubsub_broadcaster_ext.cpp
    keypair_file_storage.cpp
    ColdStorageStackStub.cpp
)

if(SGNS_COLD_STORAGE)
target_sources(crdt_globaldb PRIVATE
    ColdStorageStack.cpp
    ColdStorageManager.cpp
    BlockStoreBackedIpfsDatastore.cpp
)
target_link_libraries(crdt_globaldb PRIVATE sgns-blockstore::sgns-blockstore)
endif()

set_target_properties(crdt_globaldb PROPERTIES UNITY_BUILD ON)

target_link_libraries(crdt_globaldb
    PUBLIC
    sgns_version
    Boost::headers
    ipfs-pubsub
    crdt_datastore
    p2p::p2p_peer_address
    gossip_auth
    PRIVATE
    rapidjson
    crdt_graphsync_dagsyncer
    crdt_globaldb_proto
    ipfs-lite-cpp::ipfs_datastore_rocksdb
    Boost::filesystem
    p2p::p2p_protocol_echo
    ${WIN_CRYPT_LIBRARY}
)
supergenius_install(crdt_globaldb)
