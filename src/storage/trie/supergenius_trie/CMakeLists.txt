add_library(supergenius_node
    supergenius_node.cpp
    )
target_link_libraries(supergenius_node
    buffer
    )
supergenius_install(supergenius_node)

add_library(trie_error
    trie_error.cpp
    )
target_link_libraries(trie_error
    outcome
    )
supergenius_install(trie_error)

add_library(supergenius_trie_cursor
    supergenius_trie_cursor.cpp
    )
target_link_libraries(supergenius_trie_cursor
    buffer
    supergenius_codec
    )
supergenius_install(supergenius_trie_cursor)

add_library(supergenius_trie
    supergenius_trie_impl.cpp
    )
target_link_libraries(supergenius_trie
    supergenius_node
    trie_error
    supergenius_codec
    supergenius_trie_cursor
    )
supergenius_install(supergenius_trie)

add_library(supergenius_trie_factory
    supergenius_trie_factory_impl.cpp
    )
target_link_libraries(supergenius_trie_factory
    supergenius_trie
    )
