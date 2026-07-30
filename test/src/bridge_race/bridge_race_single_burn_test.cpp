/**
 * @file       bridge_race_single_burn_test.cpp
 * @brief      Genuine 11-validator proof that one post-readiness burn finalizes once.
 * @date       2026-07-30
 */

#include "bridge_race_fixture.hpp"

#include <algorithm>
#include <array>
#include <set>

namespace
{
    using Status = sgns::TransactionManager::TransactionStatus;

    template <std::size_t N>
    std::vector<std::shared_ptr<GeniusNode>> NodeVector(
        const std::array<std::shared_ptr<GeniusNode>, N> &nodes )
    {
        return { nodes.begin(), nodes.end() };
    }

    template <std::size_t N>
    std::vector<std::shared_ptr<sgns::ConsensusManager>> ManagerVector(
        const std::array<std::shared_ptr<sgns::ConsensusManager>, N> &managers )
    {
        return { managers.begin(), managers.end() };
    }
}

TEST_F( BridgeRaceE2ETest, ExactlyOneCertificateForOneBurn )
{
    const std::string destination = DeriveLightDestination( 1u );

    // Constructed fixture state is necessary but not sufficient: install one
    // private observer per live manager before enabling ingestion.
    ASSERT_TRUE( std::all_of( s_nodes.begin(), s_nodes.end(), []( const auto &node )
    { return node && node->GetState() == GeniusNode::NodeState::READY; } ) );
    InstallConsensusObservers();
    ASSERT_NE( s_evidence, nullptr );

    sgns::WeightedRpcEndpoint direct;
    direct.url                     = s_anvil.RpcUrl();
    direct.consensus_weight        = 100;
    direct.bridge_contract_address = sgns::test::anvil::kSepoliaBridgeContractLower;
    direct.accepted_topic0_hashes  = { sgns::test::anvil::BridgeEventTopic0() };

    auto public_one = direct;
    public_one.consensus_weight = 0;
    auto public_two = direct;
    public_two.consensus_weight = 0;
    const std::vector<sgns::WeightedRpcEndpoint> endpoints{ direct, public_one, public_two };

    // Check every result. No burn exists in the configured scan range yet.
    for ( unsigned int i = 0; i < kNodeCount; ++i )
    {
        const bool configured = s_nodes[i]->ConfigureRpcEndpoint(
            sgns::test::anvil::kSepoliaChainId, endpoints );
        s_evidence->SetEndpointResult( i, configured );
        ASSERT_TRUE( configured ) << "node=" << i << " ConfigureRpcEndpoint failed";
    }

    const bool all_ready = s_evidence->WaitForExternal(
        [&]()
        {
            bool ready = true;
            for ( unsigned int i = 0; i < kNodeCount; ++i )
            {
                const bool node_ready = s_nodes[i] &&
                                        s_nodes[i]->GetState() == GeniusNode::NodeState::READY &&
                                        s_nodes[i]->GetTransactionManagerState() == sgns::TransactionManager::State::READY;
                s_evidence->SetReady( i, node_ready );
                ready = ready && node_ready;
            }
            return ready;
        },
        kRaceNodeReadyTimeout );
    ASSERT_TRUE( all_ready ) << s_evidence->Render(
        "pre-burn", NodeVector( s_nodes ), ManagerVector( s_consensus_managers ), destination );
    ASSERT_TRUE( BridgeRaceEvidence::AllReady( s_evidence->Snapshot() ) );

    std::array<uint64_t, kNodeCount> initial_balances{};
    std::array<uint64_t, kNodeCount> initial_confirms{};
    for ( unsigned int i = 0; i < kNodeCount; ++i )
    {
        initial_balances[i] = s_nodes[i]->GetBalance( destination );
        initial_confirms[i] = sgns::BridgeRaceConsensusTestAccess::ConfirmCount( s_nodes[i] );
    }

    // Keep transport isolated only until all real watchers have published their
    // local proposal; the production PubSub/CRDT mesh is healed immediately below.
    DisconnectConsensusMesh();

    // This is the sole post-readiness burn in the fixture scan range.
    const std::string burn_hash = sgns::test::anvil::SendBridgeOutBurn(
        s_anvil.RpcUrl(), static_cast<uint64_t>( kMintAmount ), destination );
    ASSERT_FALSE( burn_hash.empty() ) << "sole bridgeOut burn failed";

    const bool all_proposals = s_evidence->WaitForSnapshot(
        []( const std::vector<BridgeRaceEvidence::Node> &snapshot )
        {
            const auto slots = BridgeRaceEvidence::ProposalSlots( snapshot );
            return slots.size() == 1 && BridgeRaceEvidence::AllLocalProposals( snapshot, *slots.begin() );
        },
        kRaceNodeReadyTimeout );
    ASSERT_TRUE( all_proposals ) << s_evidence->Render(
        "pending", NodeVector( s_nodes ), ManagerVector( s_consensus_managers ), destination );

    const auto proposal_snapshot = s_evidence->Snapshot();
    const auto slots = BridgeRaceEvidence::ProposalSlots( proposal_snapshot );
    ASSERT_EQ( slots.size(), 1u ) << "one burn must create exactly one new canonical mint slot";
    const std::string slot = *slots.begin();

    ConnectConsensusMesh();

    const bool all_proposals_replicated = s_evidence->WaitForExternal(
        [&]()
        {
            return std::all_of(
                s_consensus_managers.begin(), s_consensus_managers.end(),
                [&]( const auto &manager )
                {
                    return sgns::BridgeRaceConsensusTestAccess::ProposalCountForSlot( manager, slot ) ==
                           kNodeCount;
                } );
        },
        kRaceNodeReadyTimeout );
    ASSERT_TRUE( all_proposals_replicated ) << s_evidence->Render(
        slot, NodeVector( s_nodes ), ManagerVector( s_consensus_managers ), destination );

    // Every manager now owns the same complete candidate set. Isolate vote
    // publication so an early quorum cannot suppress a later validator's vote.
    DisconnectConsensusMesh();

    const bool one_vote_each = s_evidence->WaitForSnapshot(
        [&]( const std::vector<BridgeRaceEvidence::Node> &snapshot )
        {
            const auto targets = BridgeRaceEvidence::VoteTargets( snapshot, slot );
            return targets.size() == kNodeCount &&
                   std::all_of( targets.begin(), targets.end(), []( const auto &entry )
                   { return entry.second.size() == 1; } );
        },
        kRaceNodeReadyTimeout );
    ASSERT_TRUE( one_vote_each ) << s_evidence->Render(
        slot, NodeVector( s_nodes ), ManagerVector( s_consensus_managers ), destination );

    const auto vote_targets = BridgeRaceEvidence::VoteTargets( s_evidence->Snapshot(), slot );
    ASSERT_EQ( vote_targets.size(), kNodeCount );
    for ( const auto &[validator, targets] : vote_targets )
        ASSERT_EQ( targets.size(), 1u ) << "validator=" << validator << " published distinct usable vote targets";

    ConnectConsensusMesh();

    std::optional<sgns::BridgeRaceConsensusTestAccess::Authority> authority;
    const bool authority_converged = s_evidence->WaitForExternal(
        [&]()
        {
            std::optional<sgns::BridgeRaceConsensusTestAccess::Authority> candidate;
            for ( const auto &manager : s_consensus_managers )
            {
                auto current = sgns::BridgeRaceConsensusTestAccess::GetAuthority( manager, slot );
                if ( !current ) return false;
                if ( !candidate ) candidate = current;
                if ( current->bytes != candidate->bytes ||
                     current->proposal_id != candidate->proposal_id ||
                     current->winner_id != candidate->winner_id )
                    return false;
            }
            authority = std::move( candidate );
            return authority.has_value();
        },
        kRaceNodeReadyTimeout );
    ASSERT_TRUE( authority_converged ) << s_evidence->Render(
        slot, NodeVector( s_nodes ), ManagerVector( s_consensus_managers ), destination );
    ASSERT_TRUE( authority );

    auto proposal_subjects = BridgeRaceEvidence::ProposalSubjects( s_evidence->Snapshot(), slot );
    ASSERT_EQ( proposal_subjects.size(), kNodeCount )
        << "all validators must publish distinct local proposals for the sole burn";
    ASSERT_EQ( proposal_subjects.erase( authority->winner_id ), 1u );
    const std::set<std::string> losing_transactions = std::move( proposal_subjects );
    ASSERT_EQ( losing_transactions.size(), kNodeCount - 1u );

    const bool application_converged = s_evidence->WaitForExternal(
        [&]()
        {
            for ( unsigned int i = 0; i < kNodeCount; ++i )
            {
                if ( s_nodes[i]->GetTransactionStatus( authority->winner_id ) != Status::CONFIRMED ||
                     s_nodes[i]->GetBalance( destination ) != initial_balances[i] + kMintAmount ||
                     sgns::BridgeRaceConsensusTestAccess::ConfirmCount( s_nodes[i] ) != initial_confirms[i] + 1u ||
                     !sgns::BridgeRaceConsensusTestAccess::ProcessComplete( s_consensus_managers[i], slot ) )
                    return false;
                for ( const auto &loser : losing_transactions )
                    if ( s_nodes[i]->GetTransactionStatus( loser ) == Status::CONFIRMED ) return false;
            }
            return true;
        },
        kRaceNodeReadyTimeout );
    ASSERT_TRUE( application_converged ) << s_evidence->Render(
        slot, NodeVector( s_nodes ), ManagerVector( s_consensus_managers ), destination );

    // A condition-variable stability window resets on every relevant trace event;
    // it is an event-counter proof, not an unconditional scheduling sleep.
    ASSERT_TRUE( s_evidence->WaitForStableEvents( kRaceStabilityWindow,
                                                  kRaceStabilityWindow + kRaceNodeReadyTimeout ) )
        << s_evidence->Render(
               slot, NodeVector( s_nodes ), ManagerVector( s_consensus_managers ), destination );

    for ( unsigned int i = 0; i < kNodeCount; ++i )
    {
        EXPECT_EQ( s_nodes[i]->GetState(), GeniusNode::NodeState::READY );
        EXPECT_EQ( s_nodes[i]->GetTransactionStatus( authority->winner_id ), Status::CONFIRMED );
        EXPECT_EQ( s_nodes[i]->GetBalance( destination ), initial_balances[i] + kMintAmount );
        EXPECT_EQ( sgns::BridgeRaceConsensusTestAccess::ConfirmCount( s_nodes[i] ), initial_confirms[i] + 1u );
        EXPECT_TRUE( sgns::BridgeRaceConsensusTestAccess::ProcessComplete( s_consensus_managers[i], slot ) );
        for ( const auto &loser : losing_transactions )
            EXPECT_NE( s_nodes[i]->GetTransactionStatus( loser ), Status::CONFIRMED );
    }

    const auto final_targets = BridgeRaceEvidence::VoteTargets( s_evidence->Snapshot(), slot );
    ASSERT_EQ( final_targets.size(), kNodeCount );
    for ( const auto &[validator, targets] : final_targets )
        EXPECT_EQ( targets.size(), 1u ) << "validator=" << validator;

    spdlog::info( "bridge_race: burn={} slot={} winner={} proposals={} validators={} stable_ms={}",
                  burn_hash,
                  slot,
                  authority->winner_id,
                  kNodeCount,
                  final_targets.size(),
                  kRaceStabilityWindow.count() );
}
