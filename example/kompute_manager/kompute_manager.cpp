#include "kompute_manager.hpp"

KomputeManager::KomputeManager(const std::vector<uint32_t>& spirv)
    : m_spirv(spirv)
{    
}

void KomputeManager::Evaluate(const std::vector<float>& in, std::vector<float>& out)
{
    kp::Manager mgr;
    auto tensorIn = mgr.tensor(in);
    out = std::vector<float>(in.size(), 0.0);
    auto tensorOut = mgr.tensor(out);

    std::vector<std::shared_ptr<kp::Tensor>> params = { tensorIn, tensorOut };
    std::shared_ptr<kp::Algorithm> algo = mgr.algorithm(params, m_spirv);

    auto sq = mgr.sequence()
        ->record<kp::OpTensorSyncDevice>(params)
        ->record<kp::OpAlgoDispatch>(algo)
        ->record<kp::OpTensorSyncLocal>(params);
    sq->eval();

    out = tensorOut->vector();
}
