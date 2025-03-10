add_executable(processing_dapp
    processing_dapp.cpp
)

message(STATUS "LINK DAPP")
target_link_libraries(processing_dapp
    processing_service
    crdt_globaldb
    logger
    Boost::program_options
)
message(STATUS "END LINK DAPP")
add_executable(processing_dapp_processor
    processing_dapp_processor.cpp
)

target_link_libraries(processing_dapp_processor
    processing_service
    crdt_globaldb
    logger
    Boost::program_options
)
