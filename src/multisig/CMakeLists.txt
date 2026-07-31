add_library(multisig
    MultiSig.cpp
)
target_link_libraries(multisig
    PRIVATE
    sgns_genius_account
)
supergenius_install(multisig)
