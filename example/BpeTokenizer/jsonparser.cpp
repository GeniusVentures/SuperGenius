#include "jsonparser.hpp"

// Parse JSON string into a map
std::unordered_map<std::string, std::string> SimpleJsonParser::parseJson( const std::string &jsonString )
{
    std::unordered_map<std::string, std::string> result;

    try
    {
        rapidjson::Document doc;
        doc.Parse( jsonString.c_str() );

        if ( doc.HasParseError() || !doc.IsObject() )
        {
            std::cerr << "Error parsing JSON string" << std::endl;
            return result;
        }

        // Convert Document to map
        for ( rapidjson::Value::ConstMemberIterator itr = doc.MemberBegin(); itr != doc.MemberEnd(); ++itr )
        {
            if ( itr->name.IsString() )
            {
                std::string key = itr->name.GetString();

                if ( itr->value.IsString() )
                {
                    result[key] = itr->value.GetString();
                }
                else if ( itr->value.IsNumber() )
                {
                    if ( itr->value.IsInt() )
                    {
                        result[key] = std::to_string( itr->value.GetInt() );
                    }
                    else if ( itr->value.IsDouble() )
                    {
                        result[key] = std::to_string( itr->value.GetDouble() );
                    }
                }
                else if ( itr->value.IsBool() )
                {
                    result[key] = itr->value.GetBool() ? "true" : "false";
                }
                else if ( itr->value.IsObject() || itr->value.IsArray() )
                {
                    // Convert objects and arrays to JSON strings
                    rapidjson::StringBuffer                    buffer;
                    rapidjson::Writer<rapidjson::StringBuffer> writer( buffer );
                    itr->value.Accept( writer );
                    result[key] = buffer.GetString();
                }
            }
        }
    }
    catch ( const std::exception &e )
    {
        std::cerr << "Exception while parsing JSON: " << e.what() << std::endl;
    }

    return result;
}

// Parse JSON array from string
std::vector<std::unordered_map<std::string, std::string>> SimpleJsonParser::parseJsonArray(
    const std::string &jsonString,
    const std::string &arrayName )
{
    std::vector<std::unordered_map<std::string, std::string>> result;

    try
    {
        rapidjson::Document doc;
        doc.Parse( jsonString.c_str() );

        if ( doc.HasParseError() )
        {
            std::cerr << "Error parsing JSON string" << std::endl;
            return result;
        }

        // Check if the document has the array we're looking for
        if ( !doc.HasMember( arrayName.c_str() ) || !doc[arrayName.c_str()].IsArray() )
        {
            std::cerr << "JSON does not contain array named '" << arrayName << "'" << std::endl;
            return result;
        }

        // Get the array
        const rapidjson::Value &array = doc[arrayName.c_str()];

        // Process each element in the array
        for ( rapidjson::SizeType i = 0; i < array.Size(); i++ )
        {
            if ( array[i].IsObject() )
            {
                std::unordered_map<std::string, std::string> item;

                // Convert the object to a map
                for ( rapidjson::Value::ConstMemberIterator itr = array[i].MemberBegin(); itr != array[i].MemberEnd();
                      ++itr )
                {
                    if ( itr->name.IsString() )
                    {
                        std::string key = itr->name.GetString();

                        if ( itr->value.IsString() )
                        {
                            item[key] = itr->value.GetString();
                        }
                        else if ( itr->value.IsNumber() )
                        {
                            if ( itr->value.IsInt() )
                            {
                                item[key] = std::to_string( itr->value.GetInt() );
                            }
                            else if ( itr->value.IsDouble() )
                            {
                                item[key] = std::to_string( itr->value.GetDouble() );
                            }
                        }
                        else if ( itr->value.IsBool() )
                        {
                            item[key] = itr->value.GetBool() ? "true" : "false";
                        }
                        else if ( itr->value.IsObject() || itr->value.IsArray() )
                        {
                            // Convert objects and arrays to JSON strings
                            rapidjson::StringBuffer                    buffer;
                            rapidjson::Writer<rapidjson::StringBuffer> writer( buffer );
                            itr->value.Accept( writer );
                            item[key] = buffer.GetString();
                        }
                    }
                }

                result.push_back( item );
            }
        }
    }
    catch ( const std::exception &e )
    {
        std::cerr << "Exception while parsing JSON array: " << e.what() << std::endl;
    }

    return result;
}

// Parse a JSON file into a map
std::unordered_map<std::string, std::string> SimpleJsonParser::parseJsonFile( const std::string &filePath )
{
    try
    {
        std::ifstream file( filePath );
        if ( !file.is_open() )
        {
            std::cerr << "Failed to open JSON file: " << filePath << std::endl;
            return {};
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        return parseJson( buffer.str() );
    }
    catch ( const std::exception &e )
    {
        std::cerr << "Exception while parsing JSON file: " << e.what() << std::endl;
        return {};
    }
}

// Helper methods

int SimpleJsonParser::getIntValue( const std::unordered_map<std::string, std::string> &json,
                                   const std::string                                  &key,
                                   int                                                 defaultValue )
{
    auto it = json.find( key );
    if ( it == json.end() )
    {
        return defaultValue;
    }

    try
    {
        return std::stoi( it->second );
    }
    catch ( ... )
    {
        return defaultValue;
    }
}

std::string SimpleJsonParser::getStringValue( const std::unordered_map<std::string, std::string> &json,
                                              const std::string                                  &key,
                                              const std::string                                  &defaultValue )
{
    auto it = json.find( key );
    if ( it == json.end() )
    {
        return defaultValue;
    }
    return it->second;
}

bool SimpleJsonParser::getBoolValue( const std::unordered_map<std::string, std::string> &json,
                                     const std::string                                  &key,
                                     bool                                                defaultValue )
{
    auto it = json.find( key );
    if ( it == json.end() )
    {
        return defaultValue;
    }

    std::string value = it->second;
    return value == "true" || value == "1";
}
