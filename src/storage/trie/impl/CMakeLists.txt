

add_library(trie_storage_backend
    trie_storage_backend_impl.cpp
    trie_storage_backend_batch.cpp
    )
set_target_properties(trie_storage_backend PROPERTIES PUBLIC_HEADER "trie_storage_backend_impl.hpp;trie_storage_backend_batch.hpp")
target_link_libraries(trie_storage_backend
    buffer
    )
supergenius_install(trie_storage_backend)

add_library(topper_trie_batch
    topper_trie_batch_impl.cpp
    )
set_target_properties(topper_trie_batch PROPERTIES PUBLIC_HEADER "topper_trie_batch_impl.hpp")
target_link_libraries(topper_trie_batch
    buffer
    )
supergenius_install(topper_trie_batch)

add_library(persistent_trie_batch
    persistent_trie_batch_impl.cpp
    )
set_target_properties(persistent_trie_batch PROPERTIES PUBLIC_HEADER "persistent_trie_batch_impl.hpp")
target_link_libraries(persistent_trie_batch
    buffer
    trie_error
    supergenius_trie_cursor
    topper_trie_batch
    )
supergenius_install(persistent_trie_batch)

add_library(ephemeral_trie_batch
    ephemeral_trie_batch_impl.cpp
    )
set_target_properties(ephemeral_trie_batch PROPERTIES PUBLIC_HEADER "ephemeral_trie_batch_impl.hpp")
target_link_libraries(ephemeral_trie_batch
    buffer
    supergenius_trie_cursor
    topper_trie_batch
    )
supergenius_install(ephemeral_trie_batch)

add_library(trie_storage
    trie_storage_impl.cpp
    )
set_target_properties(trie_storage PROPERTIES PUBLIC_HEADER "trie_storage_impl.hpp")
target_link_libraries(trie_storage
    ephemeral_trie_batch
    persistent_trie_batch
    )
supergenius_install(trie_storage)
