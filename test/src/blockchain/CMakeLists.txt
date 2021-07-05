
addtest(block_header_repository_test
    block_header_repository_test.cpp
    )
target_link_libraries(block_header_repository_test
    block_header_repository
    base_rocksdb_test
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
if(FORCE_MULTILE)
  set_target_properties(block_tree_test PROPERTIES LINK_FLAGS "${MULTIPLE_OPTION}")
endif()

addtest(block_storage_test
    block_storage_test.cpp
    )
target_link_libraries(block_storage_test
    block_storage
    )
if(FORCE_MULTILE)
  set_target_properties(block_storage_test PROPERTIES LINK_FLAGS "${MULTIPLE_OPTION}")
endif()
