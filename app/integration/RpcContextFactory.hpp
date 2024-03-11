/**
 * @file       RpcContextFactory.hpp
 * @brief      
 * @date       2024-03-05
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _RPC_CONTEXT_FACTORY_HPP_
#define _RPC_CONTEXT_FACTORY_HPP_

#include "api/transport/rpc_io_context.hpp"

namespace sgns
{
    //TODO - replace boost::asio::io_context in other classes with this
    class RpcContextFactory
    {
        public:
        std::shared_ptr<api::RpcContext> create()
        {
            return std::make_shared<api::RpcContext>();
        }
    };
}

#endif
