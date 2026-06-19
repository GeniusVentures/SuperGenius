// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef TEST_MOCK_RPC_CONFIG_HPP
#define TEST_MOCK_RPC_CONFIG_HPP

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

} // namespace sgns::test

#endif // TEST_MOCK_RPC_CONFIG_HPP
