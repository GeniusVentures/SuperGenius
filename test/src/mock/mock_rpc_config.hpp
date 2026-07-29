// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef TEST_MOCK_RPC_CONFIG_HPP
#define TEST_MOCK_RPC_CONFIG_HPP

#include <array>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace sgns::test
{

    enum class MockBehavior
    {
        kSuccess,           // Return canned (or default valid) receipt
        kTimeout,           // Return std::nullopt (simulates transport timeout)
        kConnectionRefused, // Return std::nullopt (simulates refused connection)
        kBadJson,           // Return unparseable string
        kWrongStatus,       // Return receipt with status=false
        kWrongLogs          // Return receipt with mismatched logs
    };

    struct MockEndpointConfig
    {
        std::string  url;
        MockBehavior behavior = MockBehavior::kSuccess;
        // Ordered responses keyed by tx_hash for stateful sequences (D-10)
        std::map<std::string, std::vector<std::string>> responses;
    };

    /// @brief Parse a per-node JSON config file at the given path.
    /// @param config_path Path to mock_rpc_config.json.
    /// @return Vector of endpoint configs, or empty vector if the file doesn't exist or is invalid.
    std::vector<MockEndpointConfig> LoadMockConfig( const std::filesystem::path &config_path );

    /// @brief Build 3 genuinely divergent per-slot RPC endpoint mock configs (DIRECT +
    ///        2x PUBLIC), each with a distinct URL, for exercising RPC-endpoint
    ///        disagreement against the >75% weighted quorum rule (D-09).
    /// @param direct_behavior   Behavior for the "mock://direct" slot (default kSuccess).
    /// @param public1_behavior  Behavior for the "mock://public1" slot (default kWrongLogs).
    /// @param public2_behavior  Behavior for the "mock://public2" slot (default kTimeout).
    /// @return Array of exactly 3 MockEndpointConfig with distinct "mock://" URLs.
    std::array<MockEndpointConfig, 3> BuildDivergentSlotConfigs(
        MockBehavior direct_behavior  = MockBehavior::kSuccess,
        MockBehavior public1_behavior = MockBehavior::kWrongLogs,
        MockBehavior public2_behavior = MockBehavior::kTimeout );

} // namespace sgns::test

#endif // TEST_MOCK_RPC_CONFIG_HPP
