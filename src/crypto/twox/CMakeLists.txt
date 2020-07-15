

add_library(twox twox.cpp)
target_link_libraries(twox
  xxhash
    blob
  )
supergenius_install(twox)
