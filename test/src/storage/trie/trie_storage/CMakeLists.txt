addtest(supergenius_trie_storage_test
    supergenius_codec_nibbles_test.cpp
    supergenius_codec_hash256_test.cpp
    supergenius_codec_node_encoding_test.cpp
    supergenius_codec_node_decoding_test.cpp
    trie_storage_test.cpp
    trie_batch_test.cpp
    ordered_trie_hash_test.cpp
)

# target_compile_options (supergenius_trie_storage_test PRIVATE /wd4251 /wd4101)
if(FORCE_MULTIPLE)
    set_target_properties(supergenius_trie_storage_test PROPERTIES LINK_FLAGS "${MULTIPLE_OPTION}")
endif()

target_link_libraries(supergenius_trie_storage_test
    trie_storage
    rocksdb
    ordered_trie_hash
    supergenius_trie_factory
    Boost::headers
    base_rocksdb_test
    trie_storage_backend
    trie_serializer
    in_memory_storage
    trie_error
    ipfs-lite-cpp::blake2
)

addtest(trie_storage_backend_test
    trie_storage_backend_test.cpp
)

target_link_libraries(trie_storage_backend_test
    trie_storage_backend
    blob
    buffer
    in_memory_storage
)
