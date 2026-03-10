/**
 * @file       JSONSecureStorage.cpp
 * @brief      
 * @date       2024-06-06
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "JSONSecureStorage.hpp"

#include <cstdio>
#include <array>

#include <boost/filesystem.hpp>
#include <rapidjson/writer.h>
#include <rapidjson/filereadstream.h>
#include <rapidjson/filewritestream.h>

namespace sgns
{
    constexpr std::string_view FILE_NAME = "secure_storage.json";

    outcome::result<rj::Document> JSONSecureStorage::LoadJSON() const
    {
        auto fullpath = directory_ / FILE_NAME;
        auto file     = std::fopen( fullpath.generic_string().c_str(), "r" );
        if ( file == nullptr )
        {
            return outcome::failure( std::errc::no_such_file_or_directory );
        }

        std::array<char, 512>     buff{};
        rapidjson::FileReadStream input_stream( file, buff.data(), buff.size() );

        std::fclose( file );

        rapidjson::Document document;
        document.ParseStream( input_stream );
        if ( document.HasParseError() )
        {
            return outcome::failure( std::errc::bad_message );
        }

        return document;
    }

    outcome::result<JSONSecureStorage::SecureBufferType> JSONSecureStorage::Load( const std::string &key )
    {
        OUTCOME_TRY( auto document, LoadJSON() );
        auto maybe_field = document.FindMember( "GeniusAccount" );

        if ( maybe_field == document.MemberEnd() || !maybe_field->value.IsObject() )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        auto maybe_value = maybe_field->value.FindMember( key.data() );
        if ( ( maybe_value == maybe_field->value.MemberEnd() ) || ( !maybe_value->value.IsString() ) )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        SecureBufferType result;
        result.assign( maybe_value->value.GetString(), maybe_value->value.GetStringLength() );

        return result;
    }

    outcome::result<void> JSONSecureStorage::Save( const std::string &key, const SecureBufferType &buffer )
    {
        auto fullpath = directory_ / FILE_NAME;

        // Ensure the directory exists (create it if necessary)
        boost::system::error_code ec;
        boost::filesystem::create_directories( directory_, ec );
        if ( ec ) // Check for errors during directory creation
        {
            return outcome::failure( ec );
        }

        auto file = std::fopen( fullpath.generic_string().c_str(), "w" );
        if ( file == nullptr )
        {
            // Return a meaningful error code for file opening failure
            return outcome::failure( boost::system::error_code( errno, boost::system::generic_category() ) );
        }

        // Proceed with writing JSON data
        std::array<char, 512>                         buff{};
        rapidjson::FileWriteStream                    output_stream( file, buff.data(), buff.size() );
        rapidjson::Writer<rapidjson::FileWriteStream> writer( output_stream );
        writer.StartObject();
        writer.Key( "GeniusAccount" );
        writer.StartObject();
        writer.Key( key.c_str() );
        writer.String( buffer.data() );
        writer.EndObject();
        writer.EndObject();

        std::fclose( file );

        return outcome::success();
    }

    outcome::result<bool> JSONSecureStorage::DeleteKey( const std::string &key )
    {
        return outcome::failure( std::errc::operation_not_supported );
    }
}
