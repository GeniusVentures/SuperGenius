add_library(trie_storage_backend
    trie_storage_backend_impl.cpp
    trie_storage_backend_batch.cpp
)
target_link_libraries(trie_storage_backend
    PUBLIC
    buffer
)
supergenius_install(trie_storage_backend)

add_library(topper_trie_batch
    topper_trie_batch_impl.cpp
)
target_link_libraries(topper_trie_batch
    PUBLIC
    buffer
    Boost::headers
    PRIVATE
    trie_error
)
supergenius_install(topper_trie_batch)

add_library(persistent_trie_batch
    persistent_trie_batch_impl.cpp
)
target_link_libraries(persistent_trie_batch
    PUBLIC
    buffer
    changes_tracker
    trie_serializer
    PRIVATE
    supergenius_trie_cursor
    topper_trie_batch
    scale
)
supergenius_install(persistent_trie_batch)

add_library(ephemeral_trie_batch
    ephemeral_trie_batch_impl.cpp
)
target_link_libraries(ephemeral_trie_batch
    PUBLIC
    buffer
    supergenius_trie
    PRIVATE
    supergenius_trie_cursor
)
supergenius_install(ephemeral_trie_batch)

add_library(trie_storage
    trie_storage_impl.cpp
)
target_link_libraries(trie_storage
    PUBLIC
    logger
    supergenius_trie_factory
    PRIVATE
    ephemeral_trie_batch
    persistent_trie_batch
)
supergenius_install(trie_storage)
