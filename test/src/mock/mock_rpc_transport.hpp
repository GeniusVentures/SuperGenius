// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef TEST_MOCK_RPC_TRANSPORT_HPP
#define TEST_MOCK_RPC_TRANSPORT_HPP

#include <eth/rpc_receipt_source.hpp>
#include <map>
#include <string>
#include <vector>

#include "src/mock/mock_rpc_config.hpp"

namespace sgns::test
{

    class MockRpcTransport final : public eth::rpc::JsonRpcTransport
    {
    public:
        explicit MockRpcTransport( const MockEndpointConfig &config );

        [[nodiscard]] std::optional<std::string> call( const boost::json::object &request ) override;

        // Test control methods (not on JsonRpcTransport interface)
        void ResetState();
        void SetBehavior( MockBehavior b );

        size_t CallCount() const
        {
            return call_count_;
        }

    private:
        MockEndpointConfig            config_;
        size_t                        call_count_ = 0;
        std::map<std::string, size_t> response_index_; // tx_hash -> next response index
    };

} // namespace sgns::test

#endif // TEST_MOCK_RPC_TRANSPORT_HPP s
