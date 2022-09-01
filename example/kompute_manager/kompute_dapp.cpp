#include "infura_ipfs.hpp"

#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>

int main(int argc, const char* argv)
{
    // Task data preparation
    std::ifstream spvFile("array_multiplication.comp.spv", std::ios::binary);

    std::vector<char> spvData;
    spvData.insert(spvData.begin(), std::istreambuf_iterator<char>(spvFile), {});

    spvFile.close();

    std::stringstream taskData;

    // Add signature
    char signature[] = {'t', 's', 'k', '1'};
    taskData.write(signature, sizeof(signature) / sizeof(char));

    // Add spirv module
    uint64_t spvSize = spvData.size();
    taskData.write((char*)&spvSize, sizeof(uint64_t));
    taskData.write(spvData.data(), spvSize);

    // Add data to process
    std::vector<float> data({ 1.0, 3.0, 5.0, 2.0, 4.0, 6.0 });
    uint64_t dataSize = data.size();
    taskData.write((char*)&dataSize, sizeof(uint64_t));

    for (auto& val : data)
    {
        taskData.write((char*)&val, sizeof(float));
    }

    // Add task data to IPFS
    std::ifstream autorizationKeyFile("authorizationKey.txt");

    if (!autorizationKeyFile.is_open())
    {
        std::cout << "Infura authorization key file not found" << std::endl;
        return EXIT_FAILURE;
    }
    std::string autorizationKey;
    std::getline(autorizationKeyFile, autorizationKey);

    InfuraIPFS ipfs(autorizationKey);
    auto cid = ipfs.AddIPFSFile("sg_task_data.bin", taskData.str());

    std::cout << cid << std::endl;

    //std::string ipfsData;
    //ipfs.GetIPFSFile(cid, ipfsData);

    //std::ofstream f("data.ipfs");
    //f << ipfsData;
    //f.close();

    return 0;
}
