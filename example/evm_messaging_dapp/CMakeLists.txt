add_executable(evm_messaging_dapp
        evm_messaging_dapp.cpp
)

# this should include all the source headers
include_directories(
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(evm_messaging_dapp PRIVATE
        evm_watcher_service
        logger
        Boost::program_options
        Boost::thread
        Boost::chrono
        rapidjson
)
