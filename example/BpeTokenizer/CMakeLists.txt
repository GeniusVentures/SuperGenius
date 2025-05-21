add_executable(BpeTokenizer
	BpeTokenizer.hpp
    BpeEncode.cpp 
	BpeTokenizer.cpp
	jsonparser.hpp
	SplitEmbedding.hpp
	SplitLMHead.hpp
	MultiExpert.hpp
	jsonparser.cpp
	SplitEmbedding.cpp
	SplitLMHead.cpp
	MultiExpert.cpp
	Utility.hpp
	Utility.cpp
	GateWeights.hpp
	GateWeights.cpp
)
add_executable(TestEmbedding
	TestEmbedding.cpp
)
add_executable(TestExpert
	TestExpert.cpp
)
set_target_properties(BpeTokenizer PROPERTIES UNITY_BUILD ON)
set_target_properties(TestEmbedding PROPERTIES UNITY_BUILD ON)
set_target_properties(TestExpert PROPERTIES UNITY_BUILD ON)
include_directories(
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(BpeTokenizer PRIVATE
    rapidjson
	MNN::MNN
    Vulkan::Vulkan
    ${WIN_CRYPT_LIBRARY}
)
target_link_libraries(TestEmbedding PRIVATE
    rapidjson
	MNN::MNN
    Vulkan::Vulkan
    ${WIN_CRYPT_LIBRARY}
)
target_link_libraries(TestExpert PRIVATE
    rapidjson
	MNN::MNN
    Vulkan::Vulkan
    ${WIN_CRYPT_LIBRARY}
)
