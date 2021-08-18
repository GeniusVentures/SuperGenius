
add_library(storage_wasm_provider
    storage_wasm_provider.cpp
    )
target_link_libraries(storage_wasm_provider
    buffer
    )
add_library(const_wasm_provider
    const_wasm_provider.cpp
    )
set_target_properties(const_wasm_provider PROPERTIES PUBLIC_HEADER "const_wasm_provider.hpp")
target_link_libraries(const_wasm_provider
    buffer
    )

supergenius_install(const_wasm_provider)

add_library(trie_storage_provider
    trie_storage_provider_impl.cpp
    )
set_target_properties(trie_storage_provider PROPERTIES PUBLIC_HEADER "trie_storage_provider_impl.hpp")
target_link_libraries(trie_storage_provider
    trie_storage
    )
supergenius_install(trie_storage_provider)
