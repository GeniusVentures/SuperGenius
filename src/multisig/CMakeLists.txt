add_library(multisig
    MultiSig.cpp
)
target_link_libraries(multisig
    PUBLIC
    sgns_genius_account
)
supergenius_install(multisig)
