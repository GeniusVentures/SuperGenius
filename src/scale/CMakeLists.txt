add_library(scale
    scale.hpp
    scale_decoder_stream.hpp
    scale_decoder_stream.cpp
    scale_encoder_stream.hpp
    scale_encoder_stream.cpp
    scale_error.hpp
    scale_error.cpp
    types.hpp
    detail/fixed_witdh_integer.hpp
    detail/variant.hpp
    )
set_target_properties(scale PROPERTIES PUBLIC_HEADER "scale.hpp;scale_decoder_stream.hpp;scale_encoder_stream.hpp;scale_error.hpp;types.hpp;detail/fixed_witdh_integer.hpp;detail/variant.hpp")
target_link_libraries(scale
    Boost::boost
    buffer
    )


supergenius_install(scale)
