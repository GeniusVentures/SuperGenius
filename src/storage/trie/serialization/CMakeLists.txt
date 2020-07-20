

add_library(trie_serializer
    trie_serializer_impl.cpp
    )
target_link_libraries(trie_serializer
    supergenius_node
    )

set_target_properties(trie_serializer PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE")

add_library(supergenius_codec
    supergenius_codec.cpp
    )
target_link_libraries(supergenius_codec
    scale
    blake2
    blob
    supergenius_node
    )
supergenius_install(supergenius_codec)

add_library(ordered_trie_hash INTERFACE)
target_link_libraries(ordered_trie_hash INTERFACE
    supergenius_trie
    supergenius_codec
    scale
    )
supergenius_install(ordered_trie_hash)
