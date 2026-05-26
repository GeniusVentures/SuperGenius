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

#include "account/TokenID.hpp"
#include "blockchain/Blockchain.hpp"
#include "blockchain/Consensus.hpp"
#include "outcome/outcome.hpp"

namespace sgns
{
    inline constexpr std::string_view kBridgeEventSubjectType = "gnus.bridge_event.v1";

    using BridgeEventConsensusHandler =
        std::function<outcome::result<ConsensusManager::Check>( const eth::BridgeEventClaim &claim,
                                                                const ConsensusManager::Subject &subject )>;
    using BridgeEventConsensusCertificateHandler =
        std::function<outcome::result<ConsensusManager::Check>( const eth::BridgeEventClaim       &claim,
                                                                const std::string                 &subject_hash,
                                                                const ConsensusManager::Certificate &certificate )>;

    struct BridgeEventMintRequest
    {
        uint64_t    amount = 0;
        std::string transaction_hash;
        std::string chain_id;
        TokenID     token_id;
        std::string destination;
    };

    outcome::result<ConsensusManager::Subject> CreateBridgeEventConsensusSubject(
        const std::string      &account_id,
        const eth::BridgeEventClaim &claim );

    outcome::result<eth::BridgeEventClaim> DecodeBridgeEventConsensusSubject(
        const ConsensusManager::Subject &subject );

    outcome::result<BridgeEventMintRequest> CreateBridgeEventMintRequest(
        const eth::BridgeEventClaim &claim );

    ConsensusManager::SubjectHandler MakeBridgeEventConsensusHandler(
        BridgeEventConsensusHandler handler );

    ConsensusManager::CertificateSubjectHandler MakeBridgeEventConsensusCertificateHandler(
        BridgeEventConsensusCertificateHandler handler );

    bool RegisterBridgeEventConsensusHandler(
        const std::shared_ptr<Blockchain> &blockchain,
        BridgeEventConsensusHandler       handler );

    bool RegisterBridgeEventConsensusCertificateHandler(
        const std::shared_ptr<Blockchain>        &blockchain,
        BridgeEventConsensusCertificateHandler    handler );
} // namespace sgns

#endif // _BRIDGE_CONSENSUS_ADAPTER_HPP_
