add_executable(node_example
    NodeExample.cpp
    )
    include_directories(
        ${PROJECT_SOURCE_DIR}/src
        )
    target_link_libraries(node_example PRIVATE
    sgns_account
    )
target_include_directories(node_example PRIVATE ${AsyncIOManager_INCLUDE_DIR})
if(WIN32)
    target_link_options(node_example PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:sgns_account>)

elseif(APPLE)
    target_link_options(node_example PUBLIC -force_load "$<TARGET_FILE:sgns_account>")

else()
    target_link_options(node_example PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:sgns_account>"
        "-Wl,--no-whole-archive"
    )
endif()
