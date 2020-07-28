
# addtest(block_builder_test
#     block_builder_test.cpp
#     )

# target_link_libraries(block_builder_test
#     block_builder
#     blob
#     )

# set_target_properties(block_builder_test PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE")

addtest(block_builder_factory_test
    block_builder_factory_test.cpp
    )

target_link_libraries(block_builder_factory_test
    block_builder_factory
    )

set_target_properties(block_builder_factory_test PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE")

# addtest(proposer_test
#     proposer_test.cpp
#     )

# target_link_libraries(proposer_test
#     proposer
#     )

# set_target_properties(proposer_test PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE")