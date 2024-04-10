
add_proto_library(SGTransactionProto proto/SGTransaction.proto)

add_library(sgns_account
    TransferTransaction.cpp
    MintTransaction.cpp
)

target_link_libraries(sgns_account
    SGTransactionProto
    Boost::boost
)