add_executable(BpeTokenizer
	BpeTokenizer.hpp
    BpeEncode.cpp 
	BpeTokenizer.cpp
)
set_target_properties(BpeTokenizer PROPERTIES UNITY_BUILD ON)

include_directories(
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(BpeTokenizer PRIVATE
    rapidjson
    ${WIN_CRYPT_LIBRARY}
)
