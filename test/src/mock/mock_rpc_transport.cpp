// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include "src/mock/mock_rpc_transport.hpp"

#include <boost/json.hpp>
#include <fstream>
#include <optional>
#include <sstream>

namespace sgns::test
{

namespace
{

/// @brief Default valid receipt JSON for tx_hash not found in canned responses.
constexpr const char *kDefaultReceiptJson = R"({
    "jsonrpc": "2.0",
    "id": 1,
    "result": {
        "status": "0x1",
        "blockNumber": "0x100000",
        "blockHash": "0x0000000000000000000000000000000000000000000000000000000000000001",
        "transactionHash": "0x00000000000000000000000000000000000000000000000000000000000000ff",
        "logs": []
    }
})";

/// @brief Bridge contract address used in default success/wrong-status responses.
constexpr const char *kBridgeContractAddress = "0x1234567890123456789012345678901234567890";

/// @brief Bridge event topic0 used in default success responses.
constexpr const char *kBridgeEventTopic0 =
    "0x1234567890123456789012345678901234567890123456789012345678901234";

/// @brief Extract the transaction hash (hex string) from an eth_getTransactionReceipt JSON-RPC request.
/// The request has shape: {"method":"eth_getTransactionReceipt","params":["0x..."],"id":N}
/// Returns std::nullopt (rather than throwing) for any request that doesn't match this
/// shape — e.g. eth_blockNumber (no "params" key), eth_getLogs (object-shaped "params"),
/// or an empty params array — so non-receipt requests reaching this mock never crash the
/// caller with an uncaught boost::json exception.
std::optional<std::string> ExtractTxHash(const boost::json::object &request)
{
    const auto params_it = request.find("params");
    if (params_it == request.end() || !params_it->value().is_array())
    {
        return std::nullopt;
    }
    const auto &params = params_it->value().as_array();
    if (params.empty() || !params.at(0).is_string())
    {
        return std::nullopt;
    }
    return std::string(params.at(0).as_string().c_str());
}

/// @brief Build a valid receipt JSON string with the given tx_hash, status, log address, and topic0.
std::string BuildReceiptJson(
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
    result["blockNumber"] = "0x100000";
    result["blockHash"] = "0x0000000000000000000000000000000000000000000000000000000000000001";
    result["transactionHash"] = tx_hash;

    boost::json::array logs;
    boost::json::object log_entry;
    log_entry["address"] = log_address;
    log_entry["topics"] = boost::json::array{topic0};
    log_entry["data"] = "0x";
    log_entry["blockNumber"] = "0x100000";
    log_entry["blockHash"] = "0x0000000000000000000000000000000000000000000000000000000000000001";
    log_entry["transactionHash"] = tx_hash;
    log_entry["logIndex"] = "0x0";
    logs.emplace_back(std::move(log_entry));
    result["logs"] = std::move(logs);

    root["result"] = std::move(result);
    return boost::json::serialize(root);
}

/// @brief Build a default success receipt for the given tx_hash.
std::string BuildSuccessReceipt(const std::string &tx_hash)
{
    return BuildReceiptJson(tx_hash, "0x1", kBridgeContractAddress, kBridgeEventTopic0);
}

/// @brief Build a wrong-status receipt (status=0x0) for the given tx_hash.
std::string BuildWrongStatusReceipt(const std::string &tx_hash)
{
    return BuildReceiptJson(tx_hash, "0x0", kBridgeContractAddress, kBridgeEventTopic0);
}

/// @brief Build a wrong-logs receipt (mismatched address and topic0) for the given tx_hash.
std::string BuildWrongLogsReceipt(const std::string &tx_hash)
{
    return BuildReceiptJson(
        tx_hash,
        "0x1",
        "0x0000000000000000000000000000000000000000",
        "0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef");
}

/// @brief Map a behavior string from JSON config to the MockBehavior enum.
MockBehavior ParseBehaviorString(const std::string &s)
{
    if (s == "timeout")
    {
        return MockBehavior::kTimeout;
    }
    if (s == "connection_refused")
    {
        return MockBehavior::kConnectionRefused;
    }
    if (s == "bad_json")
    {
        return MockBehavior::kBadJson;
    }
    if (s == "wrong_status")
    {
        return MockBehavior::kWrongStatus;
    }
    if (s == "wrong_logs")
    {
        return MockBehavior::kWrongLogs;
    }
    return MockBehavior::kSuccess;
}

} // namespace

MockRpcTransport::MockRpcTransport(const MockEndpointConfig &config) : config_(config)
{
}

std::optional<std::string> MockRpcTransport::call(const boost::json::object &request)
{
    ++call_count_;
    const std::optional<std::string> maybe_tx_hash = ExtractTxHash(request);
    if (!maybe_tx_hash.has_value())
    {
        // Non-receipt request (e.g. eth_blockNumber, eth_getLogs) — this mock only
        // models eth_getTransactionReceipt-shaped calls; return nullopt rather than
        // guessing at a response for a request shape it doesn't understand.
        return std::nullopt;
    }
    const std::string &tx_hash = *maybe_tx_hash;

    switch (config_.behavior)
    {
    case MockBehavior::kTimeout:
        return std::nullopt;

    case MockBehavior::kConnectionRefused:
        return std::nullopt;

    case MockBehavior::kBadJson:
        return std::string{"{broken"};

    case MockBehavior::kWrongStatus:
    {
        auto it = config_.responses.find(tx_hash);
        if (it != config_.responses.end() && !it->second.empty())
        {
            std::string resp = it->second.front();
            return resp;
        }
        return BuildWrongStatusReceipt(tx_hash);
    }

    case MockBehavior::kWrongLogs:
    {
        auto it = config_.responses.find(tx_hash);
        if (it != config_.responses.end() && !it->second.empty())
        {
            std::string resp = it->second.front();
            return resp;
        }
        return BuildWrongLogsReceipt(tx_hash);
    }

    case MockBehavior::kSuccess:
    {
        auto it = config_.responses.find(tx_hash);
        if (it != config_.responses.end() && !it->second.empty())
        {
            size_t &index = response_index_[tx_hash];
            const std::string &resp = it->second[index % it->second.size()];
            ++index;
            return resp;
        }
        return BuildSuccessReceipt(tx_hash);
    }
    }

    return BuildSuccessReceipt(tx_hash);
}

void MockRpcTransport::ResetState()
{
    call_count_ = 0;
    response_index_.clear();
}

void MockRpcTransport::SetBehavior(MockBehavior b)
{
    config_.behavior = b;
}

std::vector<MockEndpointConfig> LoadMockConfig(const std::filesystem::path &config_path)
{
    std::vector<MockEndpointConfig> result;

    if (!std::filesystem::exists(config_path))
    {
        return result;
    }

    try
    {
        std::ifstream file(config_path);
        if (!file.is_open())
        {
            return result;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string json_text = buffer.str();

        const auto root = boost::json::parse(json_text);
        const auto &root_obj = root.as_object();

        const auto endpoints_it = root_obj.find("endpoints");
        if (endpoints_it == root_obj.end() || !endpoints_it->value().is_array())
        {
            return result;
        }

        for (const auto &ep_val : endpoints_it->value().as_array())
        {
            if (!ep_val.is_object())
            {
                continue;
            }

            const auto &ep_obj = ep_val.as_object();
            MockEndpointConfig config;

            const auto url_it = ep_obj.find("url");
            if (url_it != ep_obj.end() && url_it->value().is_string())
            {
                config.url = url_it->value().as_string().c_str();
            }

            const auto behavior_it = ep_obj.find("behavior");
            if (behavior_it != ep_obj.end() && behavior_it->value().is_string())
            {
                config.behavior = ParseBehaviorString(behavior_it->value().as_string().c_str());
            }

            const auto responses_it = ep_obj.find("responses");
            if (responses_it != ep_obj.end() && responses_it->value().is_object())
            {
                for (const auto &kv : responses_it->value().as_object())
                {
                    const std::string tx_hash(kv.key());
                    if (!kv.value().is_array())
                    {
                        continue;
                    }
                    std::vector<std::string> canned;
                    for (const auto &resp_val : kv.value().as_array())
                    {
                        if (resp_val.is_string())
                        {
                            canned.push_back(resp_val.as_string().c_str());
                        }
                    }
                    if (!canned.empty())
                    {
                        config.responses[tx_hash] = std::move(canned);
                    }
                }
            }

            result.push_back(std::move(config));
        }
    }
    catch (const std::exception &)
    {
        // Malformed JSON — return empty config (T-05-03 mitigation)
        return {};
    }

    return result;
}

std::array<MockEndpointConfig, 3> BuildDivergentSlotConfigs(
    MockBehavior direct_behavior,
    MockBehavior public1_behavior,
    MockBehavior public2_behavior)
{
    return {
        MockEndpointConfig{"mock://direct", direct_behavior, {}},
        MockEndpointConfig{"mock://public1", public1_behavior, {}},
        MockEndpointConfig{"mock://public2", public2_behavior, {}},
    };
}

} // namespace sgns::test
