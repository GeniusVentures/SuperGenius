
addtest(buffer_test
    buffer_test.cpp
    )
target_link_libraries(buffer_test
    buffer
    )

addtest(hexutil_test
    hexutil_test.cpp
    )
target_link_libraries(hexutil_test
    hexutil
    )

addtest(blob_test
    blob_test.cpp
    )
target_link_libraries(blob_test
  blob
  )

addtest(mp_utils_test
    mp_utils_test.cpp
    )
target_link_libraries(mp_utils_test
    mp_utils
    blob
    )


addtest(fixed_point_test
    fixed_point_test.cpp
    )
target_link_libraries(fixed_point_test
    fixed_point
)
