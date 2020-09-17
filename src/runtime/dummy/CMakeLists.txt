add_library(finality_api_dummy
    finality_api_dummy.cpp
    )
target_link_libraries(finality_api_dummy
    primitives
    )
