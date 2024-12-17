
addtest(supergenius_trie_test
    supergenius_trie_test.cpp
    )
target_link_libraries(supergenius_trie_test
    supergenius_trie_factory
    Boost::headers
    trie_serializer
    trie_error
)

addtest(supergenius_trie_cursor_test
    supergenius_trie_cursor_test.cpp
    )
target_link_libraries(supergenius_trie_cursor_test
    supergenius_trie_cursor
    supergenius_trie
    )
