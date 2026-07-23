addtest(securecrdt_interface_test
    securecrdt_interface_test.cpp
)
target_link_libraries(securecrdt_interface_test
    securecrdt
)

addtest(securecrdt_registry_test
    securecrdt_registry_test.cpp
)
target_link_libraries(securecrdt_registry_test
    securecrdt
)
