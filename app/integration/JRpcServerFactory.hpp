/**
 * @file       JRpcServerFactory.hpp
 * @brief      
 * @date       2024-03-05
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _JRPC_SERVER_FACTORY_HPP_
#define _JRPC_SERVER_FACTORY_HPP_

#include "api/jrpc/jrpc_server_impl.hpp"

namespace sgns
{
    class JRpcServerFactory
    {
        public: 
        std::shared_ptr<api::JRpcServer> create()
        {
            return std::make_shared<api::JRpcServerImpl>();
        }
    };
}

#endif
