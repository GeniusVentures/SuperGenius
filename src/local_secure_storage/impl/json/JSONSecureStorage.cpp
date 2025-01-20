/**
 * @file       JSONSecureStorage.cpp
 * @brief      
 * @date       2024-06-06
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include <cstdio>
#include <array>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/filereadstream.h>
#include <rapidjson/filewritestream.h>
#include "singleton/CComponentFactory.hpp"
#include "local_secure_storage/impl/json/JSONSecureStorage.hpp"
#include <boost/filesystem.hpp>

namespace sgns
{
    outcome::result<JSONSecureStorage::SecureBufferType> JSONSecureStorage::Load( const std::string &key,
                                                                                  const std::string  directory )
    {
        auto fullpath = directory + "secure_storage.json";
        auto file     = std::fopen( fullpath.data(), "r" );
        if ( !file )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        std::array<char, 512>     buff{};
        rapidjson::FileReadStream input_stream( file, buff.data(), buff.size() );

        std::fclose( file );

        rapidjson::Document document;
        document.ParseStream( input_stream );
        if ( document.HasParseError() )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        auto maybe_field = document.FindMember( "GeniusAccount" );

        if ( maybe_field == document.MemberEnd() )
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

    outcome::result<void> JSONSecureStorage::Save( const std::string      &key,
                                                const SecureBufferType &buffer,
                                                const std::string       directory )
    {
        auto fullpath = directory + "/secure_storage.json";

        // Ensure the directory exists (create it if necessary)
        boost::system::error_code ec;
        boost::filesystem::create_directories(directory, ec);
        if (ec) // Check for errors during directory creation
        {
            return outcome::failure(ec);
        }

        auto file = std::fopen(fullpath.c_str(), "w");
        if (!file)
        {
            // Return a meaningful error code for file opening failure
            return outcome::failure(boost::system::error_code(errno, boost::system::generic_category()));
        }

        // Proceed with writing JSON data
        std::array<char, 512>                         buff{};
        rapidjson::FileWriteStream                    output_stream(file, buff.data(), buff.size());
        rapidjson::Writer<rapidjson::FileWriteStream> writer(output_stream);
        writer.StartObject();
        writer.Key("GeniusAccount");
        writer.StartObject();
        writer.Key(key.c_str());
        writer.String(buffer.data());
        writer.EndObject();
        writer.EndObject();

        std::fclose(file);

        return outcome::success();
    }

    JSONSecureStorage JSONSecureStorage::Create()
    {
        JSONSecureStorage new_instance;
        auto              component_factory = SINGLETONINSTANCE( CComponentFactory );
        component_factory->Register( std::make_shared<JSONSecureStorage>( new_instance ), "LocalSecureStorage" );
        return new_instance;
    }

    JSONSecureStorage &JSONSecureStorage::GetInstance()
    {
        static JSONSecureStorage instance = Create();
        return instance;
    }

    void JSONSecureStorage::RegisterComponent()
    {
        (void)GetInstance();
    }
}
