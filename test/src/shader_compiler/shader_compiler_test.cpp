#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <boost/dll.hpp>

#include "shaders/shader_compiler.hpp"

namespace sgns
{
    class ShaderCompilerTest : public ::testing::Test
    {
    protected:
        static std::vector<char> LoadBytes( const std::string &filename )
        {
            std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
            std::string data_path = bin_path + "./shader_compiler/";
            std::string file_path = data_path + filename;

            std::ifstream stream( file_path, std::ios::binary );
            if ( !stream.is_open() )
            {
                return {};
            }
            return std::vector<char>( ( std::istreambuf_iterator<char>( stream ) ),
                                       std::istreambuf_iterator<char>() );
        }

        sgns::sgprocessing::ShaderCompiler compiler_;
    };

    TEST_F( ShaderCompilerTest, ValidGlslVertexCompilesAndValidates )
    {
        auto bytes = LoadBytes( "valid_vertex.glsl" );
        ASSERT_FALSE( bytes.empty() ) << "Could not load valid_vertex.glsl fixture";

        auto result =
            compiler_.CompileAndValidate( bytes, sgns::Stage::VERTEX, sgns::ShaderSourceType::GLSL, "main" );

        ASSERT_TRUE( result.has_value() ) << "Valid GLSL vertex source should compile and validate";
        EXPECT_FALSE( result.value().spirv.empty() );
    }

    TEST_F( ShaderCompilerTest, ValidGlslFragmentCompilesAndValidates )
    {
        auto bytes = LoadBytes( "valid_fragment.glsl" );
        ASSERT_FALSE( bytes.empty() ) << "Could not load valid_fragment.glsl fixture";

        auto result =
            compiler_.CompileAndValidate( bytes, sgns::Stage::FRAGMENT, sgns::ShaderSourceType::GLSL, "main" );

        ASSERT_TRUE( result.has_value() ) << "Valid GLSL fragment source should compile and validate";
        EXPECT_FALSE( result.value().spirv.empty() );
    }

    TEST_F( ShaderCompilerTest, MalformedGlslReturnsCleanCompileError )
    {
        auto bytes = LoadBytes( "malformed.glsl" );
        ASSERT_FALSE( bytes.empty() ) << "Could not load malformed.glsl fixture";

        auto result =
            compiler_.CompileAndValidate( bytes, sgns::Stage::VERTEX, sgns::ShaderSourceType::GLSL, "main" );

        ASSERT_FALSE( result.has_value() ) << "Malformed GLSL must not compile successfully";
        EXPECT_EQ( result.error(), sgns::sgprocessing::ShaderCompiler::Error::COMPILE_FAILED );
    }

    TEST_F( ShaderCompilerTest, DirectSpirvFromSuccessfulCompilePassesValidation )
    {
        auto glsl_bytes = LoadBytes( "valid_vertex.glsl" );
        ASSERT_FALSE( glsl_bytes.empty() );

        auto compiled = compiler_.CompileAndValidate( glsl_bytes, sgns::Stage::VERTEX, sgns::ShaderSourceType::GLSL,
                                                        "main" );
        ASSERT_TRUE( compiled.has_value() );

        std::vector<char> spirv_bytes( compiled.value().spirv.size() * 4 );
        std::memcpy( spirv_bytes.data(), compiled.value().spirv.data(), spirv_bytes.size() );

        auto direct_result = compiler_.CompileAndValidate( spirv_bytes, sgns::Stage::VERTEX,
                                                             sgns::ShaderSourceType::SPIRV, "main" );

        ASSERT_TRUE( direct_result.has_value() )
            << "Directly-submitted, already-valid SPIR-V bytes should pass the validation gate "
               "without recompilation";
        EXPECT_FALSE( direct_result.value().spirv.empty() );
    }

    TEST_F( ShaderCompilerTest, InvalidSpirvBytesFailValidationNotCompile )
    {
        auto bytes = LoadBytes( "invalid.spv" );
        ASSERT_FALSE( bytes.empty() ) << "Could not load invalid.spv fixture";

        auto result =
            compiler_.CompileAndValidate( bytes, sgns::Stage::FRAGMENT, sgns::ShaderSourceType::SPIRV, "main" );

        ASSERT_FALSE( result.has_value() ) << "Non-SPIR-V bytes must fail the validation gate";
        EXPECT_EQ( result.error(), sgns::sgprocessing::ShaderCompiler::Error::VALIDATION_FAILED )
            << "Rejection must come from the validation gate, not the (skipped) compile step";
    }

    TEST_F( ShaderCompilerTest, MutatedValidSpirvFailsValidation )
    {
        auto glsl_bytes = LoadBytes( "valid_vertex.glsl" );
        ASSERT_FALSE( glsl_bytes.empty() );

        auto compiled = compiler_.CompileAndValidate( glsl_bytes, sgns::Stage::VERTEX, sgns::ShaderSourceType::GLSL,
                                                        "main" );
        ASSERT_TRUE( compiled.has_value() );

        std::vector<char> spirv_bytes( compiled.value().spirv.size() * 4 );
        std::memcpy( spirv_bytes.data(), compiled.value().spirv.data(), spirv_bytes.size() );

        // Flip a byte deep inside the module (well past the 4-byte magic number) to structurally
        // corrupt it -- this proves the gate is a real structural/semantic validator, not merely a
        // magic-number check. This is the exact scenario Pitfall 2 warns about: a buffer that
        // shaderc itself produced successfully must still be rejected once corrupted.
        ASSERT_GT( spirv_bytes.size(), 16u ) << "compiled SPIR-V too small to safely mutate past the header";
        size_t mutate_index      = spirv_bytes.size() / 2;
        spirv_bytes[mutate_index] = static_cast<char>( ~spirv_bytes[mutate_index] );

        auto mutated_result = compiler_.CompileAndValidate( spirv_bytes, sgns::Stage::VERTEX,
                                                              sgns::ShaderSourceType::SPIRV, "main" );

        ASSERT_FALSE( mutated_result.has_value() )
            << "A structurally-corrupted SPIR-V module, even one shaderc originally produced "
               "successfully, must fail SPIRV-Tools validation";
        EXPECT_EQ( mutated_result.error(), sgns::sgprocessing::ShaderCompiler::Error::VALIDATION_FAILED );
    }
}
