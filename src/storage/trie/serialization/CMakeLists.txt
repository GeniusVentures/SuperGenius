

add_library(trie_serializer
    trie_serializer_impl.cpp
    )
set_target_properties(trie_serializer PROPERTIES PUBLIC_HEADER "trie_serializer.hpp;trie_serializer_impl.hpp")
target_link_libraries(trie_serializer
    supergenius_node
    )
supergenius_install(trie_serializer)
if(FORCE_MULTILE)
  set_target_properties(trie_serializer PROPERTIES LINK_FLAGS "${MULTIPLE_OPTION}")
endif()

add_library(supergenius_codec
    supergenius_codec.cpp
    )
set_target_properties(supergenius_codec PROPERTIES PUBLIC_HEADER "supergenius_codec.hpp")
target_link_libraries(supergenius_codec
    scale
    blake2
    blob
    supergenius_node
    )
supergenius_install(supergenius_codec)

add_library(ordered_trie_hash INTERFACE)
set_target_properties(ordered_trie_hash PROPERTIES PUBLIC_HEADER "ordered_trie_hash.hpp")
target_link_libraries(ordered_trie_hash INTERFACE
    supergenius_trie
    supergenius_codec
    scale
    )
supergenius_install(ordered_trie_hash)
