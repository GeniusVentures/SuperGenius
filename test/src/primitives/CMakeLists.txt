

addtest(primitives_codec_test
    primitives_codec_test.cpp
    )

target_link_libraries(primitives_codec_test
    ipfs-lite-cpp::buffer
    ipfs-lite-cpp::outcome
    primitives
    scale
    testutil_primitives_generator
    Boost::boost
    )
