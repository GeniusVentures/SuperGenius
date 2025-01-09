

addtest(primitives_codec_test
    primitives_codec_test.cpp
    )

target_link_libraries(primitives_codec_test
    buffer
    outcome
    primitives
    scale
    testutil_primitives_generator
    Boost::headers
)
