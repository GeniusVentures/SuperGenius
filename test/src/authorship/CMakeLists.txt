addtest(block_builder_test
    block_builder_test.cpp
)

target_link_libraries(block_builder_test
    block_builder
    blob
)

if(FORCE_MULTIPLE)
    set_target_properties(block_builder_test PROPERTIES LINK_FLAGS "${MULTIPLE_OPTION}")
endif()

addtest(block_builder_factory_test
    block_builder_factory_test.cpp
)

target_link_libraries(block_builder_factory_test
    Boost::headers
    block_builder_factory
)

if(FORCE_MULTIPLE)
    set_target_properties(block_builder_factory_test PROPERTIES LINK_FLAGS "${MULTIPLE_OPTION}")
endif()
