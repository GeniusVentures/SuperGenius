
add_subdirectory(impl)

add_library(extensions
    impl/extension_impl.cpp
    )
set_target_properties(extensions PROPERTIES PUBLIC_HEADER "extension.hpp;extension_factory.hpp")
target_link_libraries(extensions
    crypto_extension
    io_extension
    memory_extension
    misc_extension
    storage_extension
    )
supergenius_install(extensions)

add_library(extension_factory
    impl/extension_factory_impl.cpp
    )
target_link_libraries(extension_factory
    extensions
    )
