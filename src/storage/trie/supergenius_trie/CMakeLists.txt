add_library(supergenius_node
    supergenius_node.cpp
    )
set_target_properties(supergenius_node PROPERTIES PUBLIC_HEADER "supergenius_node.hpp")
target_link_libraries(supergenius_node
    buffer
    )
supergenius_install(supergenius_node)

add_library(trie_error
    trie_error.cpp
    )
set_target_properties(trie_error PROPERTIES PUBLIC_HEADER "trie_error.hpp")
target_link_libraries(trie_error
    outcome
    )
supergenius_install(trie_error)

add_library(supergenius_trie_cursor
    supergenius_trie_cursor.cpp
    )
set_target_properties(supergenius_trie_cursor PROPERTIES PUBLIC_HEADER "supergenius_trie_cursor.hpp")
target_link_libraries(supergenius_trie_cursor
    buffer
    supergenius_codec
    )
supergenius_install(supergenius_trie_cursor)

add_library(supergenius_trie
    supergenius_trie_impl.cpp
    )
set_target_properties(supergenius_trie PROPERTIES PUBLIC_HEADER "supergenius_trie_impl.hpp")
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
