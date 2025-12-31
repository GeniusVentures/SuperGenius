addtest(account_creation_test
    account_creation_test.cpp
)

target_include_directories(account_creation_test PRIVATE ${AsyncIOManager_INCLUDE_DIR})

target_link_libraries(account_creation_test
    genius_node
)

if(MSVC)
    target_link_options(account_creation_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node>)
elseif(APPLE)
    target_link_options(account_creation_test PUBLIC -force_load "$<TARGET_FILE:genius_node>")
else()
    target_link_options(account_creation_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node>"
        "-Wl,--no-whole-archive"
    )
endif()