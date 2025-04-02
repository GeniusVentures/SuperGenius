add_library(twox twox.cpp)
target_link_libraries(twox
    PUBLIC
    blob
    PRIVATE
    xxHash::xxhash
)
supergenius_install(twox)
