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
set_target_properties(runtime_external_interface_test PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE")