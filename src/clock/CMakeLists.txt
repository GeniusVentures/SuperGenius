

add_library(clock
    impl/clock_impl.cpp
    )

add_library(waitable_timer
    impl/basic_waitable_timer.hpp
    impl/basic_waitable_timer.cpp
    )
target_link_libraries(waitable_timer
    Boost::boost
    )
