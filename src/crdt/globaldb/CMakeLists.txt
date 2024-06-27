add_proto_library(crdt_globaldb_proto proto/broadcast.proto)

add_library(crdt_globaldb 
    globaldb.cpp
    pubsub_broadcaster.cpp
    pubsub_broadcaster_ext.cpp
    keypair_file_storage.cpp
    )

set_target_properties(crdt_globaldb PROPERTIES PUBLIC_HEADER "globaldb.hpp")

target_link_libraries(crdt_globaldb
    crdt_datastore
    crdt_graphsync_dagsyncer
    crdt_globaldb_proto
    ipfs-pubsub
    ipfs-lite-cpp::ipfs_datastore_rocksdb
    p2p::p2p_peer_address
    Boost::filesystem
    p2p::p2p_protocol_echo
    ${WIN_CRYPT_LIBRARY}
    )
supergenius_install(crdt_globaldb)