/**
 * @file       offline_chainlist.hpp
 * @brief      Offline chainlist fixture for tests that construct GeniusNodes without
 *             exercising bridge RPC.
 * @date       2026-08-26
 */
#ifndef TESTUTIL_OFFLINE_CHAINLIST_HPP
#define TESTUTIL_OFFLINE_CHAINLIST_HPP

#include <functional>
#include <optional>
#include <string>

namespace sgns::test
{
    /**
     * @brief Trimmed chainlist snapshot covering every chain in bridge_chains_config.json.
     *
     * Chain names and IDs match the real https://chainid.network/chains.json entries so the
     * parsed shape stays representative, but the RPC URLs point at a closed loopback port
     * instead of the public endpoints: callers of this fixture only need endpoints to be
     * *wired* (endpoint discovery fails closed when none are), and a refused connection
     * fails immediately rather than hanging the way an unroutable address would.
     */
    inline std::string OfflineChainlistJson()
    {
        return R"([
{"name":"Ethereum Mainnet","chainId":1,"rpc":["http://127.0.0.1:1"],"status":"active"},
{"name":"Sepolia","chainId":11155111,"rpc":["http://127.0.0.1:1"],"status":"active"},
{"name":"BNB Smart Chain Mainnet","chainId":56,"rpc":["http://127.0.0.1:1"],"status":"active"},
{"name":"BNB Smart Chain Testnet","chainId":97,"rpc":["http://127.0.0.1:1"],"status":"active"},
{"name":"Polygon Mainnet","chainId":137,"rpc":["http://127.0.0.1:1"],"status":"active"},
{"name":"Amoy","chainId":80002,"rpc":["http://127.0.0.1:1"],"status":"active"},
{"name":"Base","chainId":8453,"rpc":["http://127.0.0.1:1"],"status":"active"},
{"name":"Base Sepolia Testnet","chainId":84532,"rpc":["http://127.0.0.1:1"],"status":"active"}
])";
    }

    /**
     * @brief Fetcher to hand to GeniusNode::SetChainlistFetcher.
     *
     * GeniusNode's default fetcher performs a live ~1.1 MB HTTPS GET against
     * https://chainid.network/chains.json on the startup path, with a 15s timeout per node.
     * That makes any test constructing a node depend on a third-party service: when the
     * fetch fails, no endpoints are wired and escrow validation fails closed, so waits time
     * out for reasons unrelated to the code under test. Tests that assert bridge behaviour
     * inject their own fetcher pointing at a local chain (see bridge_race_fixture); tests
     * that merely need a working node should use this one.
     */
    inline std::function<std::optional<std::string>()> OfflineChainlistFetcher()
    {
        return []() -> std::optional<std::string> { return OfflineChainlistJson(); };
    }
} // namespace sgns::test

#endif // TESTUTIL_OFFLINE_CHAINLIST_HPP
