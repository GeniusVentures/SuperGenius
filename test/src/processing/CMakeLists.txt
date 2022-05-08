
addtest(processing_service_test
    processing_service_test.cpp
    processing_engine_test.cpp
    processing_subtask_queue_accessor_impl_test.cpp
    processing_subtask_queue_channel_pubsub_test.cpp
    processing_subtask_queue_manager_test.cpp
    )

target_include_directories(processing_service_test PRIVATE ${GSL_INCLUDE_DIR})

target_link_libraries(processing_service_test
    processing_service
    logger
    )

#if(FORCE_MULTILE)
# set_target_properties(processing_service_test PROPERTIES LINK_FLAGS "${MULTIPLE_OPTION}")
#endif()
