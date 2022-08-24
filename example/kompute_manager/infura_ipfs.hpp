#ifndef SUPERGENIUS_INFURA_IPFS_HPP
#define SUPERGENIUS_INFURA_IPFS_HPP

#include <string>

class InfuraIPFS
{
public:
    InfuraIPFS(const std::string& authorizationKey);

    void GetIPFSFile(const std::string& cid, std::string& data);
private:
    std::string m_authorizationKey;
};

#endif // SUPERGENIUS_INFURA_IPFS_HPP
