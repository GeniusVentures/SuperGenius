

addtest(hasher_test
        hasher_test.cpp
        )

target_link_libraries(hasher_test
        hasher
        buffer
        blob
        )
