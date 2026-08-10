add_library(mock_rpc_transport STATIC mock_rpc_transport.cpp)
target_link_libraries(mock_rpc_transport PUBLIC evmrelay Boost::json)

addtest(mock_rpc_test mock_rpc_test.cpp)
target_link_libraries(mock_rpc_test mock_rpc_transport)
