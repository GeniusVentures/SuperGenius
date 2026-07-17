/**
 * @file       ChainContractPair.hpp
 * @brief      Shared struct linking a chain name to its bridge contract address and numeric chain ID.
 * @date       2025-06-17
 * @author     SuperGenius
 *
 * @details    This is the shared leaf type consumed by ChainRpcEndpointProvider (as observer payload)
 *             and by BridgeRelayer (as a Start() parameter). Extracting it into its own header
 *             breaks the include cycle that would otherwise exist between those two headers when
 *             BridgeRelayer inherits IBridgeInitObserver from the provider header.
 */
#ifndef _CHAIN_CONTRACT_PAIR_HPP_
#define _CHAIN_CONTRACT_PAIR_HPP_

#include <cstdint>
#include <string>

namespace sgns
{
    /**
     * @brief Represents a chain name and its GNUS bridge contract address.
     */
    struct ChainContractPair
    {
        std::string chain_name;
        std::string contract_address;
        uint64_t    chain_id       = 0;
        uint64_t    creation_block = 0;  ///< Block at which the bridge contract was deployed (0 = unknown).
    };
} // namespace sgns

#endif
