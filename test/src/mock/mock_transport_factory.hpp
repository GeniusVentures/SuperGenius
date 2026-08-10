// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

/**
 * @file       mock_transport_factory.hpp
 * @brief      Runtime transport factory selection for test environments.
 *
 * Supports SGNS_E2E_REAL_RPC=1 env var for real RPC opt-in per D-15.
 * Default behavior: mock transport (D-14). No compile-time flags.
 */
#ifndef TEST_MOCK_TRANSPORT_FACTORY_HPP
#define TEST_MOCK_TRANSPORT_FACTORY_HPP

#include "account/PublicChainInputValidator.hpp"
#include "src/mock/mock_rpc_config.hpp"
#include "src/mock/mock_rpc_transport.hpp"

#include <eth/rpc_http_transport.hpp>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace sgns::test
{

    /**
 * @brief Check the SGNS_E2E_REAL_RPC environment variable once (D-15).
 *
 * Caches the result on first call so repeated checks return consistent
 * value throughout the test process lifetime.
 *
 * @return True when SGNS_E2E_REAL_RPC=1, false otherwise.
 */
    inline bool UseRealRpcTransport()
    {
        static const bool kUseReal = []()
        {
            const char *env = std::getenv( "SGNS_E2E_REAL_RPC" );
            return env != nullptr && std::string( env ) == "1";
        }();
        return kUseReal;
    }

    /**
 * @brief Build the appropriate TransportFactory for the current test run.
 *
 * When SGNS_E2E_REAL_RPC=1: returns a factory that creates real
 * RpcHttpTransport instances (D-15 opt-in).
 *
 * Otherwise: returns a factory that creates MockRpcTransport instances
 * configured per the provided endpoint configs (D-14 default).
 *
 * @param[in] endpoint_configs  Mock endpoint configurations (used only in mock mode).
 * @return A TransportFactory suitable for injection via SetTransportFactory().
 */
    inline sgns::TransportFactory GetTransportFactory( const std::vector<MockEndpointConfig> &endpoint_configs )
    {
        if ( UseRealRpcTransport() )
        {
            return []( const std::string   &url,
                       std::chrono::seconds timeout ) -> std::unique_ptr<eth::rpc::JsonRpcTransport>
            {
                eth::rpc::RpcHttpTransportOptions opts;
                opts.timeout = timeout;
                return std::make_unique<eth::rpc::RpcHttpTransport>( url, opts );
            };
        }

        // Default: mock transport (D-14).
        return [endpoint_configs]( const std::string &url,
                                   std::chrono::seconds /*timeout*/ ) -> std::unique_ptr<eth::rpc::JsonRpcTransport>
        {
            for ( const auto &config : endpoint_configs )
            {
                if ( config.url == url )
                {
                    return std::make_unique<MockRpcTransport>( config );
                }
            }
            // No matching config: return a default-success mock for the URL.
            MockEndpointConfig fallback;
            fallback.url      = url;
            fallback.behavior = MockBehavior::kSuccess;
            return std::make_unique<MockRpcTransport>( fallback );
        };
    }

} // namespace sgns::test
#endif // TEST_MOCK_TRANSPORT_FACTORY_HPP
