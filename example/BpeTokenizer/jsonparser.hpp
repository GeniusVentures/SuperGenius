#ifndef JSONPARSER_HPP
#define JSONPARSER_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <iostream>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

// Simple JSON parser for basic use cases
class SimpleJsonParser
{
public:
    // Parse JSON string into a map
    static std::unordered_map<std::string, std::string> parseJson( const std::string &jsonString );

    // Parse JSON array from string
    static std::vector<std::unordered_map<std::string, std::string>> parseJsonArray( const std::string &jsonString,
                                                                                     const std::string &arrayName );

    // Parse a JSON file into a map
    static std::unordered_map<std::string, std::string> parseJsonFile( const std::string &filePath );

    // Helper methods for extracting typed values

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
