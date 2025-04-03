add_library(trie_serializer
    trie_serializer_impl.cpp
)
target_link_libraries(trie_serializer
    PUBLIC
    supergenius_trie_factory
)
supergenius_install(trie_serializer)

if(FORCE_MULTIPLE)
    set_target_properties(trie_serializer PROPERTIES LINK_FLAGS "${MULTIPLE_OPTION}")
endif()

add_library(supergenius_codec
    supergenius_codec.cpp
)
target_link_libraries(supergenius_codec
    PUBLIC
    supergenius_node
    PRIVATE
    blob
    scale
    ipfs-lite-cpp::blake2
)
supergenius_install(supergenius_codec)

add_library(ordered_trie_hash INTERFACE)
target_link_libraries(ordered_trie_hash INTERFACE
    supergenius_trie
    supergenius_codec
    scale
)
supergenius_install(ordered_trie_hash)
