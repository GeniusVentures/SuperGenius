add_library(block_tree_error
    block_tree_error.hpp
    impl/block_tree_error.cpp
    )
target_link_libraries(block_tree_error
    outcome
    )

add_subdirectory(impl)

add_library(block_header_repository
    impl/key_value_block_header_repository.cpp
    impl/block_header_repository.cpp
    )
target_link_libraries(block_header_repository
    blockchain_common
    )

add_library(block_storage
    impl/key_value_block_storage.cpp
    )
target_link_libraries(block_storage
    blockchain_common
    hasher
    scale
    )
