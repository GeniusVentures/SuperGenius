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

    /// @brief Build a v2 BridgeOutInitiated success receipt whose decoded burn
    ///        facts (token id, amount, decompressed recipient) match the given
    ///        burn, for canned MockRpcTransport responses.
    ///
    /// Since MINT-04 receipt/mint fact binding, a mock success receipt must
    /// carry a decodable bridge-event log whose facts match the mint the quorum
    /// is asked to confirm — the default empty-data receipt no longer confirms
    /// a mint-v2.
    ///
    /// @param tx_hash     Burn transaction hash (0x + 64 hex chars).
    /// @param log_address Bridge contract address the endpoint expects.
    /// @param topic0      v2 BridgeOutInitiated topic0 (0x + 64 hex chars).
    /// @param token_id    Burned token id.
    /// @param amount      Burned amount.
    /// @param dest_x_hex  Bare 64-hex X half of the SGNS destination.
    /// @param dest_y_odd  Y parity of the SGNS destination.
    /// @return eth_getTransactionReceipt response JSON string.
    [[nodiscard]] std::string BuildBridgeOutFactReceiptJson( const std::string &tx_hash,
                                                             const std::string &log_address,
                                                             const std::string &topic0,
                                                             uint64_t           token_id,
                                                             uint64_t           amount,
                                                             const std::string &dest_x_hex,
                                                             bool               dest_y_odd );

} // namespace sgns::test

#endif // TEST_MOCK_RPC_TRANSPORT_HPP s
