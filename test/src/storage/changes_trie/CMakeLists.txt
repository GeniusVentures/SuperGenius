
addtest(changes_trie_test
    changes_trie_test.cpp
    )
set_target_properties(changes_trie_test PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE")
target_link_libraries(changes_trie_test
    changes_tracker
    supergenius_trie_factory
    supergenius_codec
    )

addtest(changes_tracker_test
    changes_tracker_test.cpp
    )
set_target_properties(changes_tracker_test PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE")
target_link_libraries(changes_tracker_test
    changes_tracker
    trie_storage
    trie_serializer
    supergenius_trie_factory
    trie_storage_backend
    in_memory_storage
    supergenius_codec
    )

