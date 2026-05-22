/**
 * @file       BridgeConsensusAdapter.hpp
 * @brief      Bridge-owned consensus subject helpers for EVM bridge event claims.
 */
#ifndef _BRIDGE_CONSENSUS_ADAPTER_HPP_
#define _BRIDGE_CONSENSUS_ADAPTER_HPP_

#include <functional>
#include <string>
#include <string_view>

#include <eth/bridge_event.hpp>

#include "blockchain/Blockchain.hpp"
#include "blockchain/Consensus.hpp"
#include "outcome/outcome.hpp"

namespace sgns
{
    inline constexpr std::string_view kBridgeEventSubjectType = "gnus.bridge_event.v1";

    using BridgeEventConsensusHandler =
        std::function<outcome::result<ConsensusManager::Check>( const eth::BridgeEventClaim &claim,
                                                                const ConsensusManager::Subject &subject )>;

    outcome::result<ConsensusManager::Subject> CreateBridgeEventConsensusSubject(
        const std::string      &account_id,
        const eth::BridgeEventClaim &claim );

    outcome::result<eth::BridgeEventClaim> DecodeBridgeEventConsensusSubject(
        const ConsensusManager::Subject &subject );

    ConsensusManager::SubjectHandler MakeBridgeEventConsensusHandler(
        BridgeEventConsensusHandler handler );

    bool RegisterBridgeEventConsensusHandler(
        const std::shared_ptr<Blockchain> &blockchain,
        BridgeEventConsensusHandler       handler );
} // namespace sgns

#endif // _BRIDGE_CONSENSUS_ADAPTER_HPP_
