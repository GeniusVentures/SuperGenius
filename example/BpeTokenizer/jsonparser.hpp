#ifndef JSONPARSER_HPP
#define JSONPARSER_HPP
#include <unordered_map>
#include <string>
class SimpleJsonParser
{
public:
    static std::unordered_map<std::string, std::string> parseJson( const std::string &jsonString );

    static std::vector<std::unordered_map<std::string, std::string>> parseJsonArray( const std::string &jsonString,
                                                                                     const std::string &arrayName );

    static int getIntValue( const std::unordered_map<std::string, std::string> &json,
                            const std::string                                  &key,
                            int                                                 defaultValue = 0 );

    static std::string getStringValue( const std::unordered_map<std::string, std::string> &json,
                                       const std::string                                  &key,
                                       const std::string                                  &defaultValue = "" );

    static bool getBoolValue( const std::unordered_map<std::string, std::string> &json,
                              const std::string                                  &key,
                              bool                                                defaultValue = false );
};
#endif