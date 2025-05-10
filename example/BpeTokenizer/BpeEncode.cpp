#include "BpeTokenizer.hpp"
#include <iostream>
#include <filesystem>

int main( int argc, char **argv )
{
    std::string dir = "."; // default to current directory

    if ( argc > 1 )
    {
        dir = argv[1];
    }

    std::filesystem::path vocabPath  = std::filesystem::path( dir ) / "vocab.json";
    std::filesystem::path mergesPath = std::filesystem::path( dir ) / "merges.txt";

    BpeTokenizer tokenizer;
    if ( !tokenizer.load( vocabPath.string(), mergesPath.string() ) )
    {
        std::cerr << "Failed to load tokenizer files from: " << dir << "\n";
        return 1;
    }

    std::string sentence = "The cat sat";
    auto        ids      = tokenizer.encode( sentence );

    std::cout << "Token IDs: ";
    for ( int id : ids )
    {
        std::cout << id << " ";
    }
    std::cout << std::endl;

    return 0;
}
