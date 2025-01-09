add_library(twox twox.cpp)
target_link_libraries(twox
    PUBLIC
    blob
    PRIVATE
    xxhash
)
supergenius_install(twox)
