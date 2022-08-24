#include "infura_ipfs.hpp"

#include <string>
#include <iostream>
#include <fstream>

int main(int argc, const char* argv)
{
    std::ifstream autorizationKeyFile("authorizationKey.txt");

    if (!autorizationKeyFile.is_open())
    {
        std::cout << "Infura authorization key file not found" << std::endl;
        return EXIT_FAILURE;
    }
    std::string autorizationKey;
    std::getline(autorizationKeyFile, autorizationKey);

    InfuraIPFS ipfs(autorizationKey);

    std::string data;
    ipfs.GetIPFSFile("QmQsYvj8JBa9A4wmXvxci63iz6wtLYdBE5XsRrKKxCgY9e", data);
    std::cout << data.size() << std::endl;

    std::cout << data << std::endl;
    return 0;
}