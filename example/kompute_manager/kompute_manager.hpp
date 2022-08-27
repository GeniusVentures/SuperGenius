#ifndef SUPERGENIUS_KOMPUTE_MANAGER_HPP
#define SUPERGENIUS_KOMPUTE_MANAGER_HPP

#include <kompute/Kompute.hpp>



class KomputeManager
{
public:
    KomputeManager(const std::vector<uint32_t>& spirv);
    void Evaluate(const std::vector<float>& in, std::vector<float>& out);

private:
    std::vector<uint32_t> m_spirv;
};

#endif // SUPERGENIUS_KOMPUTE_MANAGER_HPP
