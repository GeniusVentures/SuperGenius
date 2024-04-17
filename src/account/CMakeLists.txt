
add_proto_library(SGTransactionProto proto/SGTransaction.proto)

add_library(sgns_account
    TransactionManager.cpp
    TransferTransaction.cpp
    MintTransaction.cpp
    ProcessingTransaction.cpp
)

target_link_libraries(sgns_account
    SGTransactionProto
    Boost::boost
	logger
)