#ifndef SUPERGENIUS_TEST_TESTUTIL_PROCESSING_CONFORMANCE_FIXTURE_HPP
#define SUPERGENIUS_TEST_TESTUTIL_PROCESSING_CONFORMANCE_FIXTURE_HPP

#include <gtest/gtest.h>

#include <boost/dll.hpp>
#include <cstdint>
#include <fstream>
#include <map>
#include <regex>
#include <string>
#include <vector>

namespace sgns
{
    // Helper function to patch relative file:// URIs in JSON to absolute paths.
    // This ensures tests work correctly regardless of working directory.
    // Extracted from processing_datatypes_test.cpp for reuse across all conformance
    // test executables.
    static std::string PatchJsonUrisToAbsolute( const std::string &json_str, const std::string &bin_path )
    {
        std::string result;
        std::string normalized_bin_path = bin_path;

        // Normalize backslashes to forward slashes in bin_path
        for ( auto &c : normalized_bin_path )
        {
            if ( c == '\\' )
            {
                c = '/';
            }
        }

        // Pattern to match file:// URIs with relative paths (not already absolute)
        // Matches: file://path/to/file
        // Excludes: file:///absolute/path (triple slash) or file://C:/windows/path
        std::regex relative_file_uri_pattern( R"delim("(file://(?!/)(?![A-Za-z]:)[^"]+)")delim" );

        // Use regex_iterator to find and replace all matches manually
        size_t               last_pos = 0;
        std::sregex_iterator iter( json_str.begin(), json_str.end(), relative_file_uri_pattern );
        std::sregex_iterator end;

        while ( iter != end )
        {
            // Add text before the match
            result += json_str.substr( last_pos, iter->position() - last_pos );

            std::string original_uri   = ( *iter )[1].str();
            // Extract the relative part after "file://"
            std::string relative_path  = original_uri.substr( 7 ); // Skip "file://"

            // Build absolute URI using bin_path
            std::string absolute_uri = "file://" + normalized_bin_path + relative_path;

            // Add the replacement
            result += "\"" + absolute_uri + "\"";

            last_pos = iter->position() + iter->length();
            ++iter;
        }

        // Add any remaining text after the last match
        result += json_str.substr( last_pos );

        return result;
    }

    // -----------------------------------------------------------------------
    // ProcessorConformanceFixture
    // -----------------------------------------------------------------------
    // Reusable GTest fixture base class for all Phase 09 conformance test
    // executables.  Provides:
    //   - binary-relative fixture path resolution
    //   - JSON URI patching (file:// → absolute)
    //   - cached JSON loading via PatchedJson()
    //
    // Subclasses may override data_path in their own SetUpTestSuite() to
    // point at a different fixture directory (default: binary_path + "fixtures/").
    class ProcessorConformanceFixture : public ::testing::Test
    {
    protected:
        static inline std::string                        binary_path;
        static inline std::string                        data_path;
        static inline std::map<std::string, std::string> patched_json_cache = {};

        // -------------------------------------------------------------------
        // Suite-level helpers
        // -------------------------------------------------------------------

        static void SetUpTestSuite()
        {
            binary_path = boost::dll::program_location().parent_path().string() + "/";
            data_path   = binary_path + "fixtures/";
        }

        static void TearDownTestSuite() {}

        // -------------------------------------------------------------------
        // Path accessors (lazy init for subclasses that override data_path)
        // -------------------------------------------------------------------

        static const std::string &BinPath()
        {
            EnsurePaths();
            return binary_path;
        }

        static const std::string &DataPath()
        {
            EnsurePaths();
            return data_path;
        }

        // -------------------------------------------------------------------
        // JSON helpers
        // -------------------------------------------------------------------

        /// Load the named JSON fixture from DataPath(), patch relative file://
        /// URIs to absolute, and cache the result.
        static const std::string &PatchedJson( const std::string &filename )
        {
            auto cached = patched_json_cache.find( filename );
            if ( cached != patched_json_cache.end() )
            {
                return cached->second;
            }

            const std::string instance_file = DataPath() + filename;
            std::ifstream     instance_stream( instance_file );
            if ( !instance_stream.is_open() )
            {
                ADD_FAILURE() << "Failed to open instance file: " << instance_file;
                auto inserted = patched_json_cache.emplace( filename, std::string{} );
                return inserted.first->second;
            }

            std::string instance_str = std::string( std::istreambuf_iterator<char>( instance_stream ),
                                                    std::istreambuf_iterator<char>() );
            if ( instance_str.empty() )
            {
                ADD_FAILURE() << "Instance file is empty: " << instance_file;
            }

            auto inserted =
                patched_json_cache.emplace( filename, PatchJsonUrisToAbsolute( instance_str, BinPath() ) );
            return inserted.first->second;
        }

    private:
        static void EnsurePaths()
        {
            if ( binary_path.empty() )
            {
                binary_path = boost::dll::program_location().parent_path().string() + "/";
                data_path   = binary_path + "fixtures/";
            }
        }
    };

} // namespace sgns

#endif // SUPERGENIUS_TEST_TESTUTIL_PROCESSING_CONFORMANCE_FIXTURE_HPP
