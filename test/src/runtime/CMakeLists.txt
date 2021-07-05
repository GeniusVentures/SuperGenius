addtest(wasm_memory_test
    wasm_memory_test.cpp
    )
target_link_libraries(wasm_memory_test
    binaryen_wasm_memory
    )

addtest(runtime_external_interface_test
    runtime_external_interface_test.cpp
    )
target_link_libraries(runtime_external_interface_test
    binaryen_runtime_external_interface
    )
if(FORCE_MULTILE)
  set_target_properties(runtime_external_interface_test PROPERTIES LINK_FLAGS "${MULTIPLE_OPTION}")
endif()

addtest(core_integration_test
    core_integration_test.cpp
    )
target_link_libraries(core_integration_test
    binaryen_core_api
    basic_wasm_provider
    extension_factory
    ${WIN_SOCK_LIBRARY}
    ${USER_ENV_LIBRARY}
    ${WIN_CRYPT_LIBRARY}
    )
if(FORCE_MULTILE)
  set_target_properties(core_integration_test PROPERTIES LINK_FLAGS "${MULTIPLE_OPTION}")
endif()
