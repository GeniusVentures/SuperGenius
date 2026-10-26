add_proto_library(crdt_globaldb_proto proto/broadcast.proto)

# Cold-storage stack (SPILL-01, v1.1 Phase 8): EXACTLY ONE of the
# factory TUs ever compiles - the stub on default OFF builds (zero
# sgns-blockstore symbols), the real stack under SGNS_COLD_STORAGE. The
# two TUs define the SAME five symbols by design (gated factory/stub
# split), so compiling both on an ON build is an ODR violation whose
# correct binding would be left to archive-member-order luck (08-REVIEW
# CR-01).
set_source_files_properties(ColdStorageStack.cpp ColdStorageStackStub.cpp
    ColdStorageManager.cpp BlockStoreBackedIpfsDatastore.cpp
    ColdReadService.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)

add_library(crdt_globaldb
    GlobalDbNetworkComposition.cpp
    globaldb.cpp
    pubsub_broadcaster_ext.cpp
    keypair_file_storage.cpp
)

if(SGNS_COLD_STORAGE)
target_sources(crdt_globaldb PRIVATE
    ColdStorageStack.cpp
    ColdStorageManager.cpp
    BlockStoreBackedIpfsDatastore.cpp
    ColdReadService.cpp
)
target_link_libraries(crdt_globaldb PRIVATE sgns-blockstore::sgns-blockstore)
else()
target_sources(crdt_globaldb PRIVATE
    ColdStorageStackStub.cpp
)
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
