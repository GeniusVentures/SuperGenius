
addtest(processing_service_test
    processing_room_test.cpp
    processing_service_test.cpp
    processing_engine_test.cpp
    processing_subtask_queue_test.cpp
    )

target_include_directories(processing_service_test PRIVATE ${GSL_INCLUDE_DIR})

target_link_libraries(processing_service_test
    processing_service
    )

#set_target_properties(processing_service_test PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE")
