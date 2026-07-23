add_library(securecrdt INTERFACE)
target_link_libraries(securecrdt
    INTERFACE
    multisig
)
supergenius_install(securecrdt)
