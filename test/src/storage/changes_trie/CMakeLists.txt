
addtest(changes_trie_test
    changes_trie_test.cpp
    )

target_link_libraries(changes_trie_test
    changes_tracker
    supergenius_trie_factory
    supergenius_codec
    )

addtest(changes_tracker_test
    changes_tracker_test.cpp
    )

target_link_libraries(changes_tracker_test
    changes_tracker
    trie_storage
    trie_serializer
    supergenius_trie_factory
    trie_storage_backend
    in_memory_storage
    supergenius_codec
    )
