#include "infura_ipfs.hpp"
#include "kompute_manager.hpp"

#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>

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

    std::string spirvBlock;
    ipfs.GetIPFSFile("QmQsYvj8JBa9A4wmXvxci63iz6wtLYdBE5XsRrKKxCgY9e", spirvBlock);

    std::stringstream ss(spirvBlock);
    uint64_t spirvSize;
    ss.read(reinterpret_cast<char*>(&spirvSize), sizeof(uint64_t));

    std::vector<char> spirvData(spirvSize);
    ss.read(reinterpret_cast<char*>(spirvData.data()), spirvSize);

    std::vector<uint32_t> spirv(
        (uint32_t*)spirvData.data(), (uint32_t*)(spirvData.data() + spirvSize));


    uint64_t dataSize;
    ss.read(reinterpret_cast<char*>(&dataSize), sizeof(uint64_t));

    std::vector<float> dataToProcess(dataSize);
    ss.read(reinterpret_cast<char*>(dataToProcess.data()), dataSize * sizeof(float));


    std::cout << spirv.size() << std::endl;
    std::cout << dataToProcess.size() << std::endl;

    KomputeManager km(spirv);
    std::vector<float> results;
    km.Evaluate(dataToProcess, results);

    for (auto& r : results)
    {
        std::cout << r << std::endl;
    }

    //std::cout << data << std::endl;
    return 0;
}