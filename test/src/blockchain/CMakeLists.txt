
addtest(block_header_repository_test
    block_header_repository_test.cpp
    )
target_link_libraries(block_header_repository_test
    block_header_repository
    base_leveldb_test
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
set_target_properties(block_tree_test PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE")
addtest(block_storage_test
    block_storage_test.cpp
    )
target_link_libraries(block_storage_test
    block_storage
    )
set_target_properties(block_storage_test PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE")