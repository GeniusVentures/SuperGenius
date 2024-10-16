
addtest(block_header_repository_test
    block_header_repository_test.cpp
    )
target_link_libraries(block_header_repository_test
    block_header_repository
    base_rocksdb_test
    base_crdt_test
    hasher
    blockchain_common
    )

addtest(block_tree_test
    block_tree_test.cpp
    )
target_link_libraries(block_tree_test
    block_tree
    block_header_repository
    extrinsic_observer
    )

addtest(block_storage_test
    block_storage_test.cpp
    )
target_link_libraries(block_storage_test
    block_storage
    base_crdt_test
    )
