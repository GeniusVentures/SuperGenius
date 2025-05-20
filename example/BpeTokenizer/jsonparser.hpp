#ifndef JSONPARSER_HPP
#define JSONPARSER_HPP

class SimpleJsonParser
{
public:
    static std::unordered_map<std::string, std::string> parseJson( const std::string &jsonString )
    {
        std::unordered_map<std::string, std::string> result;

        // Very simple parsing for key-value pairs in JSON
        // This is not a full JSON parser, just enough for our metadata
        std::string::size_type pos = 0;
        while ( pos < jsonString.size() )
        {
            // Find next key
            pos = jsonString.find( "\"", pos );
            if ( pos == std::string::npos )
            {
                break;
            }

            std::string::size_type keyStart = pos + 1;
            pos                             = jsonString.find( "\"", keyStart );
            if ( pos == std::string::npos )
            {
                break;
            }

            std::string key = jsonString.substr( keyStart, pos - keyStart );
            pos++;

            // Find value
            pos = jsonString.find( ":", pos );
            if ( pos == std::string::npos )
            {
                break;
            }
            pos++;

            // Skip whitespace
            while ( pos < jsonString.size() && ( jsonString[pos] == ' ' || jsonString[pos] == '\t' ) )
            {
                pos++;
            }

            std::string value;
            if ( pos < jsonString.size() )
            {
                if ( jsonString[pos] == '\"' )
                {
                    // String value
                    std::string::size_type valueStart = pos + 1;
                    pos                               = jsonString.find( "\"", valueStart );
                    if ( pos == std::string::npos )
                    {
                        break;
                    }
                    value = jsonString.substr( valueStart, pos - valueStart );
                    pos++;
                }
                else if ( jsonString[pos] == '{' || jsonString[pos] == '[' )
                {
                    // Object or array - skip for now
                    int depth = 1;
                    pos++;
                    std::string::size_type valueStart = pos;
                    while ( pos < jsonString.size() && depth > 0 )
                    {
                        if ( jsonString[pos] == '{' || jsonString[pos] == '[' )
                        {
                            depth++;
                        }
                        else if ( jsonString[pos] == '}' || jsonString[pos] == ']' )
                        {
                            depth--;
                        }
                        pos++;
                    }
                    value = jsonString.substr( valueStart, pos - valueStart - 1 );
                }
                else
                {
                    // Number, boolean, or null
                    std::string::size_type valueStart = pos;
                    while ( pos < jsonString.size() && jsonString[pos] != ',' && jsonString[pos] != '}' &&
                            jsonString[pos] != ']' )
                    {
                        pos++;
                    }
                    value = jsonString.substr( valueStart, pos - valueStart );
                    value.erase( 0, value.find_first_not_of( " \t" ) );
                    value.erase( value.find_last_not_of( " \t," ) + 1 );
                }
            }

            result[key] = value;
        }

        return result;
    }

    static std::vector<std::unordered_map<std::string, std::string>> parseJsonArray( const std::string &jsonString,
                                                                                     const std::string &arrayName )
    {
        std::vector<std::unordered_map<std::string, std::string>> result;

        // Find array
        std::string            searchStr = "\"" + arrayName + "\"";
        std::string::size_type pos       = jsonString.find( searchStr );
        if ( pos == std::string::npos )
        {
            return result;
        }

        // Find array start
        pos = jsonString.find( "[", pos );
        if ( pos == std::string::npos )
        {
            return result;
        }
        pos++;

        // Parse array elements
        while ( pos < jsonString.size() )
        {
            // Skip whitespace
            while ( pos < jsonString.size() && ( jsonString[pos] == ' ' || jsonString[pos] == '\t' ||
                                                 jsonString[pos] == '\n' || jsonString[pos] == ',' ) )
            {
                pos++;
            }

            if ( pos >= jsonString.size() || jsonString[pos] == ']' )
            {
                break;
            }

            if ( jsonString[pos] == '{' )
            {
                // Found object
                std::string::size_type objectStart = pos;
                int                    depth       = 1;
                pos++;

                while ( pos < jsonString.size() && depth > 0 )
                {
                    if ( jsonString[pos] == '{' )
                    {
                        depth++;
                    }
                    else if ( jsonString[pos] == '}' )
                    {
                        depth--;
                    }
                    pos++;
                }

                if ( depth == 0 )
                {
                    std::string objectStr = jsonString.substr( objectStart + 1, pos - objectStart - 2 );
                    std::unordered_map<std::string, std::string> obj = parseJson( "{" + objectStr + "}" );
                    result.push_back( obj );
                }
            }
            else
            {
                // Skip non-object
                while ( pos < jsonString.size() && jsonString[pos] != ',' && jsonString[pos] != ']' )
                {
                    pos++;
                }
            }
        }

        return result;
    }

    static int getIntValue( const std::unordered_map<std::string, std::string> &json,
                            const std::string                                  &key,
                            int                                                 defaultValue = 0 )
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

    static std::string getStringValue( const std::unordered_map<std::string, std::string> &json,
                                       const std::string                                  &key,
                                       const std::string                                  &defaultValue = "" )
    {
        auto it = json.find( key );
        if ( it == json.end() )
        {
            return defaultValue;
        }
        return it->second;
    }

    static bool getBoolValue( const std::unordered_map<std::string, std::string> &json,
                              const std::string                                  &key,
                              bool                                                defaultValue = false )
    {
        auto it = json.find( key );
        if ( it == json.end() )
        {
            return defaultValue;
        }

        std::string value = it->second;
        return value == "true" || value == "1";
    }
};
#endif