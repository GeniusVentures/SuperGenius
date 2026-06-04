// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include "src/mock/mock_rpc_config.hpp"
#include "src/mock/mock_rpc_transport.hpp"

#include <gtest/gtest.h>

#include <eth/json_rpc.hpp>
#include <fstream>

using namespace sgns::test;

namespace
{

/// @brief Build a Hash256 with all bytes set to the given value.
rlp::Hash256 MakeTxHash(uint8_t b)
{
    rlp::Hash256 h{};
    h.fill(b);
    return h;
}

/// @brief Canonical bridge contract address used in default receipts.
const std::string kBridgeContractAddress = "0x1234567890123456789012345678901234567890";

/// @brief Canonical bridge event topic0 used in default receipts.
const std::string kBridgeEventTopic0 = "0x1234567890123456789012345678901234567890123456789012345678901234";

/// @brief Build a valid eth_getTransactionReceipt JSON-RPC response with the given tx_hash, status, log
/// address, and topic0.
std::string MakeCannedReceipt(
    const std::string &tx_hash,
    const std::string &status,
    const std::string &log_address,
    const std::string &topic0)
{
    boost::json::object root;
    root["jsonrpc"] = "2.0";
    root["id"] = 1;

    boost::json::object result;
    result["status"] = status;
    result["blockNumber"] = "0x10";
    result["blockHash"] =
        "0x0000000000000000000000000000000000000000000000000000000000000001";
    result["transactionHash"] = tx_hash;

    boost::json::array logs;
    boost::json::object log_entry;
    log_entry["address"] = log_address;
    log_entry["topics"] = boost::json::array{topic0};
    log_entry["data"] = "0x";
    log_entry["blockNumber"] = "0x10";
    log_entry["blockHash"] =
        "0x0000000000000000000000000000000000000000000000000000000000000001";
    log_entry["transactionHash"] = tx_hash;
    log_entry["logIndex"] = "0x0";
    logs.emplace_back(std::move(log_entry));
    result["logs"] = std::move(logs);

    root["result"] = std::move(result);
    return boost::json::serialize(root);
}

/// @brief Hex-encode an array of bytes with "0x" prefix.
std::string HexEncode(const rlp::Hash256 &hash)
{
    static const char kHex[] = "0123456789abcdef";
    std::string result = "0x";
    result.reserve(66);
    for (uint8_t b : hash)
    {
        result.push_back(kHex[b >> 4]);
        result.push_back(kHex[b & 0x0f]);
    }
    return result;
}

/// @brief Parse a hex string "0x..." into bytes stored in a Hash256.
rlp::Hash256 ParseHex(const std::string &s)
{
    rlp::Hash256 h{};
    if (s.size() >= 2 && s[0] == '0' && s[1] == 'x')
    {
        for (size_t i = 2; i < s.size() && (i - 2) / 2 < 32; i += 2)
        {
            char high = s[i];
            char low = s[i + 1 < s.size() ? i + 1 : i];
            auto hex_val = [](char c) -> uint8_t {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return 0;
            };
            h[(i - 2) / 2] = (hex_val(high) << 4) | hex_val(low);
        }
    }
    return h;
}

/// @brief Parse a hex string "0x..." into an Address (20 bytes).
rlp::Address ParseAddress(const std::string &s)
{
    rlp::Address a{};
    if (s.size() >= 2 && s[0] == '0' && s[1] == 'x')
    {
        for (size_t i = 2; i < s.size() && (i - 2) / 2 < 20; i += 2)
        {
            char high = s[i];
            char low = s[i + 1 < s.size() ? i + 1 : i];
            auto hex_val = [](char c) -> uint8_t {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return 0;
            };
            a[(i - 2) / 2] = (hex_val(high) << 4) | hex_val(low);
        }
    }
    return a;
}

} // namespace

class MockRpcTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        tx_hash_aa = MakeTxHash(0xaa);
        tx_hash_bb = MakeTxHash(0xbb);
        tx_hash_hex = HexEncode(tx_hash_aa);
    }

    MockEndpointConfig MakeConfig(MockBehavior b, const std::map<std::string, std::vector<std::string>> &resp = {})
    {
        MockEndpointConfig config;
        config.url = "http://test.example.com:8545";
        config.behavior = b;
        config.responses = resp;
        return config;
    }

    rlp::Hash256   tx_hash_aa{};
    rlp::Hash256   tx_hash_bb{};
    std::string    tx_hash_hex;
};

/// @given a MockRpcTransport configured with kSuccess and a canned valid receipt
/// @when call() is invoked with an eth_getTransactionReceipt request for the matching tx_hash
/// @then the response has_value() is true and parse_transaction_receipt_response() succeeds
TEST_F(MockRpcTest, SuccessReceipt)
{
    const std::string canned =
        MakeCannedReceipt(tx_hash_hex, "0x1", kBridgeContractAddress, kBridgeEventTopic0);
    MockEndpointConfig config = MakeConfig(MockBehavior::kSuccess, {{tx_hash_hex, {canned}}});
    MockRpcTransport   transport(config);

    const auto request = eth::rpc::make_get_transaction_receipt_request(tx_hash_aa, 1);
    const auto response = transport.call(request);

    ASSERT_TRUE(response.has_value());
    const auto receipt = eth::rpc::parse_transaction_receipt_response(response.value());
    ASSERT_TRUE(receipt.has_value());
    EXPECT_TRUE(receipt->receipt.status.value());
    EXPECT_EQ(receipt->tx_hash, tx_hash_aa);
}

/// @given a MockRpcTransport configured with kTimeout
/// @when call() is invoked
/// @then the response is std::nullopt
TEST_F(MockRpcTest, Timeout)
{
    MockEndpointConfig config = MakeConfig(MockBehavior::kTimeout);
    MockRpcTransport   transport(config);

    const auto request = eth::rpc::make_get_transaction_receipt_request(tx_hash_aa, 1);
    const auto response = transport.call(request);

    EXPECT_FALSE(response.has_value());
}

/// @given a MockRpcTransport configured with kConnectionRefused
/// @when call() is invoked
/// @then the response is std::nullopt
TEST_F(MockRpcTest, ConnectionRefused)
{
    MockEndpointConfig config = MakeConfig(MockBehavior::kConnectionRefused);
    MockRpcTransport   transport(config);

    const auto request = eth::rpc::make_get_transaction_receipt_request(tx_hash_aa, 1);
    const auto response = transport.call(request);

    EXPECT_FALSE(response.has_value());
}

/// @given a MockRpcTransport configured with kBadJson
/// @when call() is invoked
/// @then the response has_value() is true but parse_transaction_receipt_response() returns nullopt
TEST_F(MockRpcTest, BadJson)
{
    MockEndpointConfig config = MakeConfig(MockBehavior::kBadJson);
    MockRpcTransport   transport(config);

    const auto request = eth::rpc::make_get_transaction_receipt_request(tx_hash_aa, 1);
    const auto response = transport.call(request);

    ASSERT_TRUE(response.has_value());
    const auto receipt = eth::rpc::parse_transaction_receipt_response(response.value());
    EXPECT_FALSE(receipt.has_value());
}

/// @given a MockRpcTransport configured with kWrongStatus
/// @when call() is invoked
/// @then parse_transaction_receipt_response() succeeds but receipt.status.value() is false
TEST_F(MockRpcTest, WrongStatus)
{
    MockEndpointConfig config = MakeConfig(MockBehavior::kWrongStatus);
    MockRpcTransport   transport(config);

    const auto request = eth::rpc::make_get_transaction_receipt_request(tx_hash_aa, 1);
    const auto response = transport.call(request);

    ASSERT_TRUE(response.has_value());
    const auto receipt = eth::rpc::parse_transaction_receipt_response(response.value());
    ASSERT_TRUE(receipt.has_value());
    EXPECT_FALSE(receipt->receipt.status.value());
}

/// @given a MockRpcTransport configured with kWrongLogs
/// @when call() is invoked
/// @then parse_transaction_receipt_response() succeeds but the log address does not match the bridge
/// contract address
TEST_F(MockRpcTest, WrongLogs)
{
    MockEndpointConfig config = MakeConfig(MockBehavior::kWrongLogs);
    MockRpcTransport   transport(config);

    const auto request = eth::rpc::make_get_transaction_receipt_request(tx_hash_aa, 1);
    const auto response = transport.call(request);

    ASSERT_TRUE(response.has_value());
    const auto receipt = eth::rpc::parse_transaction_receipt_response(response.value());
    ASSERT_TRUE(receipt.has_value());
    EXPECT_TRUE(receipt->receipt.status.value());
    EXPECT_FALSE(receipt->receipt.logs.empty());

    // The log address should NOT match the expected bridge contract address
    const auto &log_entry = receipt->receipt.logs[0];
    rlp::Address expected_addr = ParseAddress(kBridgeContractAddress);
    EXPECT_NE(log_entry.address, expected_addr);
}

/// @given a MockRpcTransport with kSuccess and multiple canned responses for the same tx_hash
/// @when call() is invoked three times
/// @then the first returns resp1, second resp2, third wraps back to resp1 (modulo)
TEST_F(MockRpcTest, StatefulSequence)
{
    const std::string resp1 =
        MakeCannedReceipt(tx_hash_hex, "0x1", kBridgeContractAddress, kBridgeEventTopic0);
    const std::string resp2 =
        MakeCannedReceipt(tx_hash_hex, "0x1", kBridgeContractAddress,
                          "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    MockEndpointConfig config =
        MakeConfig(MockBehavior::kSuccess, {{tx_hash_hex, {resp1, resp2}}});
    MockRpcTransport transport(config);

    const auto request = eth::rpc::make_get_transaction_receipt_request(tx_hash_aa, 1);

    // First call: resp1
    auto response1 = transport.call(request);
    ASSERT_TRUE(response1.has_value());
    EXPECT_EQ(response1.value(), resp1);

    // Second call: resp2 — different topic0
    auto response2 = transport.call(request);
    ASSERT_TRUE(response2.has_value());
    EXPECT_EQ(response2.value(), resp2);

    // Third call: wraps back to resp1 (modulo)
    auto response3 = transport.call(request);
    ASSERT_TRUE(response3.has_value());
    EXPECT_EQ(response3.value(), resp1);
}

/// @given a MockRpcTransport
/// @when call() is invoked 3 times
/// @then CallCount() returns 3
TEST_F(MockRpcTest, CallCount)
{
    MockEndpointConfig config = MakeConfig(MockBehavior::kTimeout);
    MockRpcTransport   transport(config);

    const auto request = eth::rpc::make_get_transaction_receipt_request(tx_hash_aa, 1);
    transport.call(request);
    transport.call(request);
    transport.call(request);

    EXPECT_EQ(transport.CallCount(), 3);
}

/// @given a MockRpcTransport with stateful responses
/// @when call() is invoked once, then ResetState(), then call() again
/// @then after reset the response is resp1 again
TEST_F(MockRpcTest, ResetState)
{
    const std::string resp1 =
        MakeCannedReceipt(tx_hash_hex, "0x1", kBridgeContractAddress, kBridgeEventTopic0);
    const std::string resp2 =
        MakeCannedReceipt(tx_hash_hex, "0x1", kBridgeContractAddress,
                          "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    MockEndpointConfig config =
        MakeConfig(MockBehavior::kSuccess, {{tx_hash_hex, {resp1, resp2}}});
    MockRpcTransport transport(config);

    const auto request = eth::rpc::make_get_transaction_receipt_request(tx_hash_aa, 1);

    // Call once: resp1
    auto response1 = transport.call(request);
    ASSERT_TRUE(response1.has_value());
    EXPECT_EQ(response1.value(), resp1);

    // Reset state
    transport.ResetState();

    // After reset, should get resp1 again
    auto response2 = transport.call(request);
    ASSERT_TRUE(response2.has_value());
    EXPECT_EQ(response2.value(), resp1);

    // Call count should also be reset
    EXPECT_EQ(transport.CallCount(), 1);
}

/// @given a MockRpcTransport configured with kSuccess but no canned response for the tx_hash
/// @when call() is invoked with a tx_hash not in the responses map
/// @then a default valid receipt JSON is returned and parse_transaction_receipt_response() succeeds
TEST_F(MockRpcTest, DefaultSuccessWhenTxHashNotFound)
{
    MockEndpointConfig config = MakeConfig(MockBehavior::kSuccess);
    MockRpcTransport   transport(config);

    const auto request = eth::rpc::make_get_transaction_receipt_request(tx_hash_aa, 1);
    const auto response = transport.call(request);

    ASSERT_TRUE(response.has_value());
    // Should be parseable as a valid receipt
    const auto receipt = eth::rpc::parse_transaction_receipt_response(response.value());
    ASSERT_TRUE(receipt.has_value());
    EXPECT_TRUE(receipt->receipt.status.value());
}

/// @given a MockRpcTransport configured with kSuccess
/// @when call() is invoked for different tx_hashes
/// @then each tx_hash has an independent response_index_ (interleaving checks)
TEST_F(MockRpcTest, IndependentResponseIndices)
{
    const std::string hex_aa = tx_hash_hex;
    const std::string hex_bb = HexEncode(tx_hash_bb);
    const std::string resp_aa1 =
        MakeCannedReceipt(hex_aa, "0x1", kBridgeContractAddress, kBridgeEventTopic0);
    const std::string resp_aa2 =
        MakeCannedReceipt(hex_aa, "0x1", kBridgeContractAddress,
                          "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    const std::string resp_bb1 =
        MakeCannedReceipt(hex_bb, "0x1", kBridgeContractAddress, kBridgeEventTopic0);
    const std::string resp_bb2 =
        MakeCannedReceipt(hex_bb, "0x1", kBridgeContractAddress,
                          "0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    MockEndpointConfig config = MakeConfig(
        MockBehavior::kSuccess,
        {{hex_aa, {resp_aa1, resp_aa2}}, {hex_bb, {resp_bb1, resp_bb2}}});
    MockRpcTransport transport(config);

    const auto request_aa = eth::rpc::make_get_transaction_receipt_request(tx_hash_aa, 1);
    const auto request_bb = eth::rpc::make_get_transaction_receipt_request(tx_hash_bb, 1);

    // Interleave calls for both tx_hashes
    auto r1 = transport.call(request_aa);
    auto r2 = transport.call(request_bb);
    auto r3 = transport.call(request_aa);
    auto r4 = transport.call(request_bb);

    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());
    ASSERT_TRUE(r3.has_value());
    ASSERT_TRUE(r4.has_value());

    EXPECT_EQ(r1.value(), resp_aa1); // aa: first → resp_aa1
    EXPECT_EQ(r2.value(), resp_bb1); // bb: first → resp_bb1
    EXPECT_EQ(r3.value(), resp_aa2); // aa: second → resp_aa2
    EXPECT_EQ(r4.value(), resp_bb2); // bb: second → resp_bb2
}

/// @given a valid mock_rpc_config.json written to a temp directory
/// @when LoadMockConfig() is called with the config file path
/// @then the returned vector contains the expected MockEndpointConfig entries
TEST_F(MockRpcTest, LoadConfigFile)
{
    const std::string config_json = R"({
        "endpoints": [
            {
                "url": "http://alice.example.com:8545",
                "behavior": "success",
                "responses": {}
            },
            {
                "url": "http://bob.example.com:8545",
                "behavior": "timeout",
                "responses": {
                    "0xabc": ["{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}"]
                }
            },
            {
                "url": "http://charlie.example.com:8545",
                "behavior": "wrong_status"
            }
        ]
    })";

    const auto tmp_path = std::filesystem::temp_directory_path() / "mock_rpc_config_test.json";
    {
        std::ofstream out(tmp_path);
        ASSERT_TRUE(out.is_open());
        out << config_json;
    }

    const auto configs = LoadMockConfig(tmp_path);
    std::filesystem::remove(tmp_path);

    ASSERT_EQ(configs.size(), 3);

    EXPECT_EQ(configs[0].url, "http://alice.example.com:8545");
    EXPECT_EQ(configs[0].behavior, MockBehavior::kSuccess);
    EXPECT_TRUE(configs[0].responses.empty());

    EXPECT_EQ(configs[1].url, "http://bob.example.com:8545");
    EXPECT_EQ(configs[1].behavior, MockBehavior::kTimeout);
    EXPECT_EQ(configs[1].responses.size(), 1);

    EXPECT_EQ(configs[2].url, "http://charlie.example.com:8545");
    EXPECT_EQ(configs[2].behavior, MockBehavior::kWrongStatus);
}

/// @given a non-existent config file path
/// @when LoadMockConfig() is called
/// @then an empty vector is returned (graceful skip)
TEST_F(MockRpcTest, LoadConfigMissingFile)
{
    const auto tmp_path = std::filesystem::temp_directory_path() / "nonexistent_mock_config.json";
    const auto configs = LoadMockConfig(tmp_path);
    EXPECT_TRUE(configs.empty());
}

/// @given a mock_rpc_config.json with malformed JSON
/// @when LoadMockConfig() is called
/// @then an empty vector is returned (T-05-03 mitigation)
TEST_F(MockRpcTest, LoadConfigMalformedJson)
{
    const auto tmp_path = std::filesystem::temp_directory_path() / "malformed_config_test.json";
    {
        std::ofstream out(tmp_path);
        ASSERT_TRUE(out.is_open());
        out << "{this is not valid json";
    }

    const auto configs = LoadMockConfig(tmp_path);
    std::filesystem::remove(tmp_path);

    EXPECT_TRUE(configs.empty());
}

/// @given a MockRpcTransport configured with kSuccess
/// @when SetBehavior(kTimeout) is called and then call() is invoked
/// @then the response follows the new behavior (nullopt)
TEST_F(MockRpcTest, SetBehaviorRuntimeOverride)
{
    MockEndpointConfig config = MakeConfig(MockBehavior::kSuccess);
    MockRpcTransport   transport(config);

    // First call: success
    const auto request = eth::rpc::make_get_transaction_receipt_request(tx_hash_aa, 1);
    auto response1 = transport.call(request);
    ASSERT_TRUE(response1.has_value());

    // Override behavior
    transport.SetBehavior(MockBehavior::kTimeout);

    // Second call: timeout (runtime override)
    auto response2 = transport.call(request);
    EXPECT_FALSE(response2.has_value());
}
