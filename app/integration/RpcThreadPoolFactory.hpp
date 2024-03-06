/**
 * @file       RpcThreadPoolFactory.hpp
 * @brief      
 * @date       2024-03-05
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _RPC_THREAD_POOL_FACTORY_HPP_
#define _RPC_THREAD_POOL_FACTORY_HPP_

#include "api/transport/rpc_thread_pool.hpp"
#include "api/transport/rpc_io_context.hpp"
#include "integration/RpcContextFactory.hpp"

class CComponentFactory;
namespace sgns
{
    class RpcThreadPoolFactory
    {
    public:
        std::shared_ptr<api::RpcThreadPool> create()
        {
            auto component_factory = SINGLETONINSTANCE( CComponentFactory );

            /*auto result = component_factory->GetComponent("RpcContext", boost::none);
            if (!result)
            {
                throw std::runtime_error("Initialize RpcContext first");
            }
            auto rpc_context = std::dynamic_pointer_cast<api::RpcContext>(result.value());
            */
            auto rpc_context = RpcContextFactory{}.create();

            return std::make_shared<api::RpcThreadPool>(rpc_context, api::RpcThreadPool::Configuration{});
        }
    };
}
#endif
