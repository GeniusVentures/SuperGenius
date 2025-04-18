add_library(block_tree_error
    block_tree_error.hpp
    impl/block_tree_error.cpp
)
target_link_libraries(block_tree_error
    PUBLIC
    outcome
)
supergenius_install(block_tree_error)

add_subdirectory(impl)

add_library(block_header_repository
    impl/key_value_block_header_repository.cpp
    impl/block_header_repository.cpp
)
target_link_libraries(block_header_repository
    PUBLIC
    primitives
    logger
    crdt_globaldb
    hasher
    PRIVATE
    SGBlocksProto
)
supergenius_install(block_header_repository)

add_library(block_storage
    impl/key_value_block_storage.cpp
)
target_link_libraries(block_storage
    PUBLIC
    buffer
    logger
    hasher
    block_header_repository
    PRIVATE
    SGBlocksProto
    blockchain_common
    scale
)
supergenius_install(block_storage)
