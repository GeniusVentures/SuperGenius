#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <set>

class BpeTokenizer
{
public:
    bool load( const std::string &vocabPath, const std::string &mergesPath );

    // Tokenize input text into token IDs
    std::vector<int> encode( const std::string &input ) const;

    // Decode token IDs back into text
    std::string decode( const std::vector<int> &ids ) const;

private:
    std::unordered_map<std::string, int> vocab;
    std::unordered_map<int, std::string> id_to_token;

    struct PairHash
    {
        size_t operator()( const std::pair<std::string, std::string> &p ) const
        {
            return std::hash<std::string>()( p.first ) ^ ( std::hash<std::string>()( p.second ) << 1 );
        }
    };

    std::unordered_map<std::pair<std::string, std::string>, int, PairHash> mergeRanks;

    std::vector<std::string>        bpe( const std::string &word ) const;
    static std::vector<std::string> splitIntoBytes( const std::string &word );
    static std::string              utf8ByteChar( unsigned char byte );
};
