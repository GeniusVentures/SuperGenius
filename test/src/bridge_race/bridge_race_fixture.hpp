/**
 * @file       bridge_race_fixture.hpp
 * @brief      Phase 8 11-node (1 Full + 10 Light) mint-race e2e fixture, reused by all
 *             bridge_race test binaries.
 * @date       2026-07-16
 * @author     Henrique A. Klein (hklein@gnus.ai)
 *
 * SetUpTestSuite bootstraps an 11-node cluster (index 0 = Full node, indices 1-10 =
 * Light nodes) against a local Anvil fork of Sepolia, exactly like
 * BridgeAnvilCatchupE2ETest but WITHOUT calling ConfigureRpcEndpoint — that call is the
 * race-window trigger and is deliberately left to each TEST_F body (D-03: seed burns
 * before ConfigureRpcEndpoint, then release all 11 nodes' RPC endpoints together with no
 * per-node waits in between).
 *
 * Node identity keys are derived PROGRAMMATICALLY at runtime via DeriveNodeKey() (D-06)
 * rather than a hardcoded key literal array — only Anvil account #0 (the burn sender)
 * needs on-chain funding; the 11 node identities are libp2p/consensus keys and need none.
 */
#ifndef SUPERGENIUS_TEST_BRIDGE_RACE_FIXTURE_HPP
#define SUPERGENIUS_TEST_BRIDGE_RACE_FIXTURE_HPP

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <openssl/sha.h>
#include <boost/dll.hpp>
#include <spdlog/spdlog.h>

#include <ProofSystem/EthereumKeyGenerator.hpp>
#include "account/ChainContractPair.hpp"
#include "account/GeniusAccount.hpp"
#include "account/GeniusNode.hpp"
#include "blockchain/Blockchain.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"

#include "testutil/wait_condition.hpp"

#include "../bridge_e2e/anvil_fixture.hpp"

using sgns::GeniusNode;

/**
 * @brief Narrow friend-only bridge-race access to the real consensus graph.
 *
 * Every returned owner is a copied shared_ptr from the production ownership
 * graph. No raw lifetime-bearing pointer escapes this accessor.
 */
namespace sgns
{
class BridgeRaceConsensusTestAccess
{
public:
    using TraceEvent = ConsensusManager::ConsensusTraceEvent;

    struct Authority
    {
        std::string bytes;
        std::string proposal_id;
        std::string winner_id;
    };

    struct ApplicationOutput
    {
        uint64_t    transaction_amount{ 0 };
        std::string transaction_destination;
        uint64_t    live_amount{ 0 };
        std::string live_owner;
        bool        live_ready{ false };
        bool        owner_indexed{ false };
        int         live_state{ -1 };
    };

    static std::shared_ptr<sgns::ConsensusManager> Manager(
        const std::shared_ptr<GeniusNode> &node )
    {
        if ( !node ) return {};
        auto transaction_manager = node->GetTransactionManager();
        if ( !transaction_manager || !transaction_manager.value() ) return {};
        auto blockchain = transaction_manager.value()->blockchain_;
        return blockchain ? blockchain->consensus_manager_ : nullptr;
    }

    static void SetObserver(
        const std::shared_ptr<ConsensusManager> &manager,
        std::function<void( const TraceEvent & )> observer )
    {
        if ( manager ) manager->consensus_trace_observer_ = std::move( observer );
    }

    static std::optional<Authority> GetAuthority(
        const std::shared_ptr<ConsensusManager> &manager,
        const std::string                             &slot )
    {
        if ( !manager ) return std::nullopt;
        auto certificate = manager->GetCertificateBySlotId( slot );
        if ( !certificate ) return std::nullopt;
        auto winner = ConsensusManager::GetSubjectHash(
            certificate.value().proposal().subject() );
        if ( !winner ) return std::nullopt;
        return Authority{ certificate.value().SerializeAsString(),
                          certificate.value().proposal_id(),
                          winner.value() };
    }

    static bool ProcessComplete(
        const std::shared_ptr<ConsensusManager> &manager,
        const std::string                             &slot )
    {
        if ( !manager ) return false;
        auto process = manager->state_store_->GetProcess( slot );
        return process && process.value() &&
               process.value()->state() == sgns::ConsensusStateStore::ProcessRecord::COMPLETE;
    }

    static std::size_t ProposalCountForSlot(
        const std::shared_ptr<ConsensusManager> &manager,
        const std::string                       &slot )
    {
        if ( !manager ) return 0u;
        std::lock_guard lock( manager->proposals_mutex_ );
        return static_cast<std::size_t>( std::count_if(
            manager->proposals_.begin(), manager->proposals_.end(),
            [&]( const auto &entry ) { return entry.second.slot_key == slot; } ) );
    }

    static uint64_t ConfirmCount( const std::shared_ptr<GeniusNode> &node )
    {
        if ( !node ) return 0;
        auto transaction_manager = node->GetTransactionManager();
        return transaction_manager && transaction_manager.value()
                 ? transaction_manager.value()->metrics_tracking_confirm_.load()
                 : 0;
    }

    static void ShutdownNode( const std::shared_ptr<GeniusNode> &node )
    {
        if ( node ) node->ShutdownForDestruction();
    }

    static std::optional<ApplicationOutput> OutputFor(
        const std::shared_ptr<GeniusNode> &node,
        const std::string                 &transaction_hash )
    {
        if ( !node ) return std::nullopt;
        auto transaction_manager = node->GetTransactionManager();
        if ( !transaction_manager || !transaction_manager.value() ) return std::nullopt;
        auto manager = transaction_manager.value();
        auto tx = manager->GetTransactionByHash( transaction_hash );
        if ( !tx ) return std::nullopt;
        auto params = tx->GetUTXOParametersOpt();
        if ( !params || params->second.size() != 1u ) return std::nullopt;
        ApplicationOutput output;
        output.transaction_amount = params->second.front().encrypted_amount;
        output.transaction_destination = params->second.front().dest_address;
        auto hash = base::Hash256::fromReadableString( transaction_hash );
        if ( !hash ) return output;
        auto live = manager->account_m->GetUTXOManager().GetUnconsumedUTXO( hash.value(), 0u );
        if ( live )
        {
            output.live_amount = live->GetAmount();
            output.live_owner = live->GetOwnerAddress();
            const auto all = manager->account_m->GetUTXOManager().GetAllUTXOs();
            auto owner = all.find( output.live_owner );
            if ( owner != all.end() )
            {
                for ( const auto &[state, utxo] : owner->second )
                {
                    if ( utxo.GetTxID() == hash.value() && utxo.GetOutputIdx() == 0u )
                    {
                        output.live_state = static_cast<int>( state );
                        output.live_ready = state == UTXOManager::UTXOState::UTXO_READY;
                        break;
                    }
                }
            }
            const auto indexed = manager->account_m->GetUTXOManager().GetUnconsumedUTXOs(
                output.live_owner );
            output.owner_indexed = std::any_of(
                indexed.begin(), indexed.end(), [&]( const auto &utxo )
                { return utxo.GetTxID() == hash.value() && utxo.GetOutputIdx() == 0u; } );
        }
        return output;
    }
};
} // namespace sgns

/** Thread-safe, bounded structured evidence for the genuine 11-manager race. */
class BridgeRaceEvidence
{
public:
    using TraceEvent = sgns::BridgeRaceConsensusTestAccess::TraceEvent;
    using Clock      = std::chrono::steady_clock;

    struct Event
    {
        TraceEvent::Stage stage;
        std::string       validator_id;
        std::string       slot_id;
        std::string       proposal_id;
        std::string       subject_hash;
        std::string       payload_digest;
        std::string       delivery_source;
        uint64_t          occurrences{ 1 };
        Clock::time_point first_seen;
        Clock::time_point last_seen;
    };

    struct Node
    {
        std::optional<bool> endpoint_configured;
        bool                ready{ false };
        std::vector<Event>  events;
    };

    explicit BridgeRaceEvidence( std::size_t node_count ) : nodes_( node_count ) {}

    void Record( std::size_t node_index, const TraceEvent &event )
    {
        const auto now = Clock::now();
        std::lock_guard lock( mutex_ );
        if ( node_index >= nodes_.size() ) return;
        auto &events = nodes_[node_index].events;
        auto existing = std::find_if(
            events.begin(), events.end(), [&]( const Event &entry )
            {
                return entry.stage == event.stage &&
                       entry.validator_id == event.validator_id &&
                       entry.slot_id == event.slot_id &&
                       entry.proposal_id == event.proposal_id &&
                       entry.subject_hash == event.subject_hash &&
                       entry.payload_digest == event.payload_digest &&
                       entry.delivery_source == DeliverySourceName( event.delivery_source );
            } );
        if ( existing != events.end() )
        {
            ++existing->occurrences;
            existing->last_seen = now;
        }
        else if ( events.size() < kMaxEventsPerNode )
        {
            events.push_back( { event.stage,
                                event.validator_id,
                                event.slot_id,
                                event.proposal_id,
                                event.subject_hash,
                                event.payload_digest,
                                DeliverySourceName( event.delivery_source ),
                                1,
                                now,
                                now } );
        }
        ++change_counter_;
        cv_.notify_all();
    }

    void SetEndpointResult( std::size_t node_index, bool configured )
    {
        std::lock_guard lock( mutex_ );
        if ( node_index < nodes_.size() ) nodes_[node_index].endpoint_configured = configured;
        ++change_counter_;
        cv_.notify_all();
    }

    void SetReady( std::size_t node_index, bool ready )
    {
        std::lock_guard lock( mutex_ );
        if ( node_index < nodes_.size() ) nodes_[node_index].ready = ready;
        ++change_counter_;
        cv_.notify_all();
    }

    template <typename Predicate>
    bool WaitForSnapshot( Predicate predicate, std::chrono::milliseconds timeout ) const
    {
        const auto deadline = Clock::now() + timeout;
        std::unique_lock lock( mutex_ );
        return cv_.wait_until( lock, deadline, [&]() { return predicate( nodes_ ); } );
    }

    template <typename Predicate>
    bool WaitForExternal( Predicate predicate, std::chrono::milliseconds timeout ) const
    {
        const auto deadline = Clock::now() + timeout;
        while ( Clock::now() < deadline )
        {
            if ( predicate() ) return true;
            std::unique_lock lock( mutex_ );
            cv_.wait_until( lock, std::min( deadline, Clock::now() + std::chrono::milliseconds( 100 ) ) );
        }
        return predicate();
    }

    bool WaitForStableEvents( std::chrono::milliseconds stable_window,
                              std::chrono::milliseconds timeout ) const
    {
        const auto deadline = Clock::now() + timeout;
        std::unique_lock lock( mutex_ );
        auto observed = change_counter_;
        auto stable_since = Clock::now();
        while ( Clock::now() < deadline )
        {
            const auto stable_deadline = std::min( deadline, stable_since + stable_window );
            if ( !cv_.wait_until( lock, stable_deadline, [&]() { return change_counter_ != observed; } ) )
                return Clock::now() >= stable_since + stable_window;
            observed = change_counter_;
            stable_since = Clock::now();
        }
        return false;
    }

    std::vector<Node> Snapshot() const
    {
        std::lock_guard lock( mutex_ );
        return nodes_;
    }

    static bool AllReady( const std::vector<Node> &nodes )
    {
        return std::all_of( nodes.begin(), nodes.end(), []( const Node &node )
        {
            return node.endpoint_configured.value_or( false ) && node.ready;
        } );
    }

    static std::set<std::string> ProposalSlots( const std::vector<Node> &nodes )
    {
        std::set<std::string> slots;
        for ( const auto &node : nodes )
            for ( const auto &event : node.events )
                if ( event.stage == TraceEvent::Stage::LocalProposalPublished ) slots.insert( event.slot_id );
        return slots;
    }

    static bool AllLocalProposals( const std::vector<Node> &nodes, const std::string &slot )
    {
        return std::all_of( nodes.begin(), nodes.end(), [&]( const Node &node )
        {
            return std::any_of( node.events.begin(), node.events.end(), [&]( const Event &event )
            { return event.stage == TraceEvent::Stage::LocalProposalPublished && event.slot_id == slot; } );
        } );
    }

    static std::map<std::string, std::set<std::pair<std::string, std::string>>> VoteTargets(
        const std::vector<Node> &nodes,
        const std::string       &slot )
    {
        std::map<std::string, std::set<std::pair<std::string, std::string>>> targets;
        for ( const auto &node : nodes )
            for ( const auto &event : node.events )
                if ( event.stage == TraceEvent::Stage::VotePublished && event.slot_id == slot )
                    targets[event.validator_id].emplace( event.proposal_id, event.payload_digest );
        return targets;
    }

    static std::set<std::string> ProposalSubjects( const std::vector<Node> &nodes,
                                                   const std::string       &slot )
    {
        std::set<std::string> subjects;
        for ( const auto &node : nodes )
            for ( const auto &event : node.events )
                if ( event.stage == TraceEvent::Stage::LocalProposalPublished && event.slot_id == slot )
                    subjects.insert( event.subject_hash );
        return subjects;
    }

    std::string Render( const std::string                                  &slot,
                        const std::vector<std::shared_ptr<GeniusNode>>     &nodes,
                        const std::vector<std::shared_ptr<sgns::ConsensusManager>> &managers,
                        const std::string                                  &destination ) const
    {
        const auto snapshot = Snapshot();
        const auto subjects = ProposalSubjects( snapshot, slot );
        std::ostringstream out;
        out << "\nbridge-race structured evidence slot=" << Abbrev( slot ) << '\n';
        for ( std::size_t i = 0; i < snapshot.size(); ++i )
        {
            out << "node=" << i
                << " endpoint=" << ( snapshot[i].endpoint_configured ? ( *snapshot[i].endpoint_configured ? "ok" : "failed" ) : "unset" )
                << " ready=" << snapshot[i].ready;
            if ( i < nodes.size() && nodes[i] )
            {
                out << " balance=" << nodes[i]->GetBalance( destination )
                    << " confirms=" << sgns::BridgeRaceConsensusTestAccess::ConfirmCount( nodes[i] );
                for ( const auto &subject : subjects )
                    out << " tx[" << Abbrev( subject ) << "]="
                        << StatusName( nodes[i]->GetTransactionStatus( subject ) );
            }
            if ( i < managers.size() )
            {
                auto authority = sgns::BridgeRaceConsensusTestAccess::GetAuthority( managers[i], slot );
                out << " authority=" << ( authority ? Abbrev( authority->winner_id ) : "none" )
                    << " process_complete=" << sgns::BridgeRaceConsensusTestAccess::ProcessComplete( managers[i], slot );
                if ( authority && i < nodes.size() )
                {
                    auto output = sgns::BridgeRaceConsensusTestAccess::OutputFor(
                        nodes[i], authority->winner_id );
                    if ( output )
                        out << " output_amount=" << output->transaction_amount
                            << " output_dest=" << Abbrev( output->transaction_destination )
                            << " dest_match=" << ( output->transaction_destination == destination )
                            << " live_ready=" << output->live_ready
                            << " live_state=" << output->live_state
                            << " live_amount=" << output->live_amount
                            << " live_owner=" << Abbrev( output->live_owner )
                            << " owner_match=" << ( output->live_owner == destination )
                            << " owner_indexed=" << output->owner_indexed;
                }
            }
            out << '\n';
            const std::size_t begin = snapshot[i].events.size() > kRenderedEventsPerNode
                                        ? snapshot[i].events.size() - kRenderedEventsPerNode
                                        : 0;
            for ( std::size_t j = begin; j < snapshot[i].events.size(); ++j )
            {
                const auto &event = snapshot[i].events[j];
                out << "  stage=" << StageName( event.stage )
                    << " validator=" << Abbrev( event.validator_id )
                    << " proposal=" << Abbrev( event.proposal_id )
                    << " subject=" << Abbrev( event.subject_hash )
                    << " digest=" << Abbrev( event.payload_digest )
                    << " source=" << event.delivery_source
                    << " occurrences=" << event.occurrences << '\n';
            }
        }
        return out.str();
    }

private:
    static constexpr std::size_t kMaxEventsPerNode      = 128;
    static constexpr std::size_t kRenderedEventsPerNode = 12;

    static std::string DeliverySourceName( const std::optional<sgns::ConsensusManager::DeliverySource> &source )
    {
        if ( !source ) return "none";
        switch ( *source )
        {
            case sgns::ConsensusManager::DeliverySource::Local: return "local";
            case sgns::ConsensusManager::DeliverySource::PubSub: return "pubsub";
            case sgns::ConsensusManager::DeliverySource::CRDT: return "crdt";
            case sgns::ConsensusManager::DeliverySource::Recovery: return "recovery";
        }
        return "unknown";
    }

    static const char *StageName( TraceEvent::Stage stage )
    {
        switch ( stage )
        {
            case TraceEvent::Stage::LocalProposalPublished: return "proposal";
            case TraceEvent::Stage::VotePublished: return "vote";
            case TraceEvent::Stage::AuthorityEstablished: return "authority";
        }
        return "unknown";
    }

    static std::string Abbrev( const std::string &value )
    {
        return value.size() <= 16 ? value : value.substr( 0, 16 );
    }

    static const char *StatusName( sgns::TransactionManager::TransactionStatus status )
    {
        using Status = sgns::TransactionManager::TransactionStatus;
        switch ( status )
        {
            case Status::CREATED: return "created";
            case Status::SENDING: return "sending";
            case Status::CONFIRMED: return "confirmed";
            case Status::VERIFYING: return "verifying";
            case Status::UNCONFIRMED: return "unconfirmed";
            case Status::FAILED: return "failed";
            case Status::INVALID: return "invalid";
        }
        return "unknown";
    }

    mutable std::mutex              mutex_;
    mutable std::condition_variable cv_;
    std::vector<Node>               nodes_;
    uint64_t                        change_counter_{ 0 };
};

/**
 * @brief Reusable 11-node (1 Full + 10 Light) fixture for the mint-race e2e suite.
 *
 * Derived test files add TEST_F bodies that seed burn(s) BEFORE calling
 * ConfigureRpcEndpoint on all 11 nodes back-to-back (no waits in between), proving the
 * watcher-driven exactly-once mint invariant holds under genuine concurrent discovery.
 */
class BridgeRaceE2ETest : public ::testing::Test
{
protected:
    /** @brief Number of GeniusNode instances in the cluster (index 0 = Full node). */
    static constexpr unsigned int kNodeCount = 11u;

    /** @brief Developer payout address (DevConfig::Addr) shared by all race-test nodes. */
    static constexpr std::string_view kDevPayoutAddr = "0xcafe";

    /** @brief Developer cut fraction (DevConfig::Cut) shared by all race-test nodes. */
    static constexpr std::string_view kDevCutFraction = "0.65";

    /** @brief Child-token conversion rate in GNUS (DevConfig::TokenValueInGNUS) shared by all race-test nodes. */
    static constexpr std::string_view kDevTokenValue = "1.0";

    /** @brief Base mint amount per burn (base units). */
    static constexpr unsigned int kMintAmount = 1u;

    /** @brief PubSub port base for the race fixture (next free block after catchup suite's 40031-40033). */
    static constexpr unsigned int kNodePortBase = 40041u;

    /**
     * @brief Node READY timeout (D-15/Pitfall 2 — 11-node startup cost exceeds the
     *        3-node catchup suite's 60000ms; bump initial budget, adjust upward if
     *        measured runs exceed it).
     */
    static constexpr std::chrono::milliseconds kRaceNodeReadyTimeout{ 120000 };

    /**
     * @brief Stability window a test waits after observing the expected mint, to catch
     *        a delayed double-mint on the watcher's next poll cycle (> production poll
     *        interval).
     */
    static constexpr std::chrono::milliseconds kRaceStabilityWindow{ 16000 };

    /**
     * @brief Real configured pre-vote window for the end-to-end participation proof.
     *
     * The production watcher polls every 15 seconds. The compiled 500 ms default
     * lets the first watcher finalize before the other ten can publish, which
     * cannot exercise D-02. Sixty seconds preserves about 18 seconds of measured
     * margin over the real mesh's roughly 42-second partition replay.
     */
    static inline constexpr std::chrono::milliseconds kRaceVoteSelectionWindow{ 60000 };

    /**
     * @brief Per-node config — index 0 is the Full node, indices 1-10 are Light nodes.
     *
     * Declared `inline static` (C++17) so this header-only fixture stays ODR-safe when
     * `#include`d by multiple bridge_race test binaries — no out-of-class definition
     * needed/permitted.
     */
    static inline std::array<GeniusNodeConfig, kNodeCount> s_configs{};

    /** @brief All 11 node instances (index 0 = Full node, 1-10 = Light nodes). */
    static inline std::array<std::shared_ptr<GeniusNode>, kNodeCount> s_nodes{};

    /** Shared owners and collector installed before the sole burn is triggered. */
    static inline std::array<std::shared_ptr<sgns::ConsensusManager>, kNodeCount> s_consensus_managers{};
    static inline std::shared_ptr<BridgeRaceEvidence> s_evidence{};

    static inline sgns::test::anvil::AnvilProcess s_anvil{};

    /** @brief Anvil block captured after funding and before any test burn is submitted. */
    static inline uint64_t s_pre_burn_block = 0ull;

    /** @brief Per-node bridge config filename (must match ResolveBridgeChainsConfigPath priority 1). */
    static constexpr std::string_view kBridgeChainsConfigFilename = "bridge_chains_config.json";

    /** @brief Content of the per-node bridge_chains_config.json (sepolia-only subset). */
    static constexpr std::string_view kBridgeChainsConfigTemplate = R"JSON({
    "ethereum-sepolia": {
        "chain_id": 11155111,
        "bridge_contract_address": "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70",
        "creation_block": __CREATION_BLOCK__
    }
}
)JSON";

    /**
     * @brief Writes a per-node bridge_chains_config.json pointing BridgeCatchupWatcher at
     *        the local Anvil fork (mirrors BridgeAnvilCatchupE2ETest::WriteBridgeChainsConfig).
     *
     * Without this file, a node's BridgeCatchupWatcher/BridgeRelayer fall back to their
     * default production chain list (real mainnet/Sepolia/etc. RPC endpoints) — a burn
     * seeded only on the local Anvil fork is then never observed by ANY node, and every
     * race test times out waiting for a mint that never starts. This is the actual
     * watcher-discovery wiring D-01 requires; ConfigureRpcEndpoint() alone only affects
     * the separate PublicChainInputValidator verification-quorum path.
     *
     * @param[in] base_write_path  Per-node BaseWritePath (trailing slash expected).
     */
    static void WriteBridgeChainsConfig( const std::string &base_write_path )
    {
        std::filesystem::create_directories( base_write_path );
        const std::string config_path = base_write_path + std::string( kBridgeChainsConfigFilename );

        // Race tests must expose only burns created by the current test run. The
        // scan floor is inclusive, so start one block after the pre-burn baseline.
        // Unlike the dedicated catch-up suite, this must not import history.
        const uint64_t creation_block = s_pre_burn_block + 1ull;

        std::string        config_json( kBridgeChainsConfigTemplate );
        const std::string  placeholder( "__CREATION_BLOCK__" );
        const auto         pos = config_json.find( placeholder );
        if ( pos != std::string::npos )
        {
            config_json.replace( pos, placeholder.size(), std::to_string( creation_block ) );
        }

        std::ofstream out( config_path, std::ios::binary | std::ios::trunc );
        out << config_json;
        out.close();
        spdlog::info( "bridge_race: wrote {} (creation_block={})", config_path, creation_block );
    }

    static void WriteRaceNetworkConfig( const std::string &base_write_path, unsigned int port_seed )
    {
        std::filesystem::create_directories( base_write_path );
        std::ofstream out( base_write_path + "/network_config.json", std::ios::binary | std::ios::trunc );
        ASSERT_TRUE( out.good() ) << "Could not write bridge-race network_config.json";
        out << "{ \"port_seed\": " << port_seed
            << ", \"auto_dht\": true, \"consensus_vote_selection_window_ms\": "
            << kRaceVoteSelectionWindow.count() << " }";
        out.close();
    }

    /**
     * @brief Deterministically derives a valid secp256k1 hex private key (no 0x prefix,
     *        64 hex chars) for the given node index (D-06 — programmatic, not a
     *        hardcoded literal array).
     *
     * Derivation: SHA-256("bridge_race_node_key_seed" || index), re-hashed with a
     * counter suffix on the (astronomically unlikely) all-zero digest. Deterministic:
     * the same index always yields the same key within and across runs; distinct
     * indices yield distinct keys.
     *
     * @param[in] index  Node index (0 = Full node, 1-10 = Light nodes).
     * @return 64-hex-char private key string (no 0x prefix).
     */
    static std::string DeriveNodeKey( unsigned int index )
    {
        static constexpr const char *kSeed = "bridge_race_node_key_seed";

        for ( unsigned int attempt = 0u; attempt < 8u; ++attempt )
        {
            const std::string input = std::string( kSeed ) + std::to_string( index ) + "#" + std::to_string( attempt );
            std::vector<unsigned char> input_bytes( input.begin(), input.end() );
            std::vector<unsigned char> digest( SHA256_DIGEST_LENGTH );
            SHA256( input_bytes.data(), input_bytes.size(), digest.data() );

            bool all_zero = true;
            for ( unsigned char byte : digest )
            {
                if ( byte != 0u )
                {
                    all_zero = false;
                    break;
                }
            }
            if ( all_zero )
            {
                continue; // re-hash with a different attempt suffix (never observed in practice)
            }

            std::string hex;
            hex.reserve( digest.size() * 2u );
            static constexpr char kHexDigits[] = "0123456789abcdef";
            for ( unsigned char byte : digest )
            {
                hex.push_back( kHexDigits[( byte >> 4u ) & 0x0Fu] );
                hex.push_back( kHexDigits[byte & 0x0Fu] );
            }
            return hex;
        }
        // Unreachable in practice — SHA-256 producing an all-zero digest 8 times running
        // is not something any test run will encounter.
        return std::string( SHA256_DIGEST_LENGTH * 2u, '1' );
    }

    /**
     * @brief Returns the SGNS destination address for a Light-node index (D-07 — burns
     *        must target Light-node addresses, not the Full node's own address).
     *
     * Must read the LIVE node's GetAddress() — a node's true SGNS address is NOT simply
     * EthereumKeyGenerator(private_key). GeniusAccount::GenerateGeniusAddress() signs a
     * predefined constant with the raw key and SHA-256s the signature to derive the
     * actual key seed, so the address can only be obtained from a constructed GeniusNode.
     *
     * @param[in] light_index  Light-node index (1-10).
     * @return SGNS destination string for that Light node's identity.
     */
    static std::string DeriveLightDestination( unsigned int light_index )
    {
        return s_nodes[light_index]->GetAddress();
    }

    static void InstallConsensusObservers()
    {
        s_evidence = std::make_shared<BridgeRaceEvidence>( kNodeCount );
        for ( unsigned int i = 0; i < kNodeCount; ++i )
        {
            s_consensus_managers[i] = sgns::BridgeRaceConsensusTestAccess::Manager( s_nodes[i] );
            ASSERT_NE( s_consensus_managers[i], nullptr ) << "Missing consensus manager for node " << i;
            std::weak_ptr<BridgeRaceEvidence> weak_evidence = s_evidence;
            sgns::BridgeRaceConsensusTestAccess::SetObserver(
                s_consensus_managers[i],
                [weak_evidence, i]( const sgns::BridgeRaceConsensusTestAccess::TraceEvent &event )
                {
                    if ( auto evidence = weak_evidence.lock() ) evidence->Record( i, event );
                } );
        }
    }

    static void ClearConsensusObservers()
    {
        for ( auto &manager : s_consensus_managers )
        {
            sgns::BridgeRaceConsensusTestAccess::SetObserver( manager, {} );
            manager.reset();
        }
        s_evidence.reset();
    }

    /** Isolates the real consensus mesh while every watcher publishes locally. */
    static void DisconnectConsensusMesh()
    {
        const auto full_id = s_nodes[0]->GetPubSub()->GetHost()->getId();
        for ( unsigned int i = 1; i < kNodeCount; ++i )
        {
            const auto light_id = s_nodes[i]->GetPubSub()->GetHost()->getId();
            s_nodes[0]->GetPubSub()->GetHost()->disconnect( light_id );
            s_nodes[i]->GetPubSub()->GetHost()->disconnect( full_id );
        }
    }

    /** Heals the real star mesh through production PubSub peer admission. */
    static void ConnectConsensusMesh()
    {
        const std::string full_address = s_nodes[0]->GetPubSub()->GetLocalAddress();
        for ( unsigned int i = 1; i < kNodeCount; ++i )
        {
            const std::string light_address = s_nodes[i]->GetPubSub()->GetLocalAddress();
            s_nodes[i]->GetPubSub()->AddPeers( { full_address } );
            s_nodes[0]->GetPubSub()->AddPeers( { light_address } );
        }
    }

    /**
     * @brief Starts Anvil, funds account #0, and bootstraps the 11-node cluster.
     *
     * Deliberately does NOT call ConfigureRpcEndpoint — that call is the race-window
     * trigger and belongs in each TEST_F body (D-03).
     */
    static void SetUpTestSuite()
    {
        sgns::GeniusAccount::SetSecureStorageFactory(
            []( const std::string &identifier ) -> std::shared_ptr<sgns::ISecureStorage>
            { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );

        // D-16: skip cleanly when Foundry binaries are missing.
        if ( !sgns::test::anvil::AnvilAvailable() || !sgns::test::anvil::CastAvailable() )
        {
            GTEST_SKIP() << "Install Foundry (anvil + cast): https://book.getfoundry.sh/getting-started/installation";
        }

        const std::string fork_url = sgns::test::anvil::SepoliaForkUrl();
        spdlog::info( "bridge_race: fork_url={}", fork_url );

        ASSERT_TRUE( s_anvil.Start( fork_url ) ) << "Failed to start anvil subprocess";
        ASSERT_TRUE( s_anvil.WaitForReady() ) << "Anvil did not become ready";

        if ( !sgns::test::anvil::FundAccount0WithGnus( s_anvil.RpcUrl() ) )
        {
            s_anvil.Stop();
            GTEST_SKIP() << "Could not fund Anvil account #0 via impersonation of "
                         << sgns::test::anvil::kGnusHolderSepolia << " — skipping";
        }

        // Capture the clean pre-burn baseline. Race-test watchers start here so they
        // see burns mined after setup without importing historical fork events.
        {
            int               exit_code      = 0;
            const std::string pre_burn_block_str = sgns::test::anvil::RunShellCapture(
                "cast block-number --rpc-url " + s_anvil.RpcUrl(), exit_code );
            ASSERT_EQ( exit_code, 0 ) << "Could not query Anvil fork block via cast block-number";
            ASSERT_FALSE( pre_burn_block_str.empty() ) << "cast block-number returned empty output";
            s_pre_burn_block = std::stoull( pre_burn_block_str );
            ASSERT_GT( s_pre_burn_block, 0ull ) << "Anvil pre-burn block must be non-zero";
            spdlog::info( "bridge_race: pre-burn baseline block = {}", s_pre_burn_block );
        }

        const std::string binary_path = boost::dll::program_location().parent_path().string();

        const std::string kAnvilRpcUrl = s_anvil.RpcUrl();
        auto chainlist_fetcher = [kAnvilRpcUrl]() -> std::optional<std::string>
        {
            return std::string( R"([{"name":"ethereum-sepolia","chainId":11155111,"rpc":[")" ) +
                   kAnvilRpcUrl + R"("],"status":"active"}])";
        };

        // Derive every real SGNS address through GeniusAccount before constructing a
        // GeniusNode. Blockchain initialization is asynchronous, so registering either
        // genesis list after node construction leaves a genuine race in which the full
        // node can try to create the registry before its authority address is available.
        // Proactively remove any stale per-node data directory left over from a PRIOR run
        // that didn't exit cleanly (e.g. a crash/SEGFAULT skips TearDownTestSuite's own
        // remove_all entirely). Without this, a fresh run can start against stale
        // RocksDB/CRDT state from an old process, producing confusing, nondeterministic
        // behavior that has nothing to do with the current run's actual logic.
        for ( unsigned int i = 0u; i < kNodeCount; ++i )
        {
            const std::string stale_path = binary_path + "/bridge_race_node" + std::to_string( i + 1u ) + "/";
            std::error_code   ec;
            std::filesystem::remove_all( stale_path, ec );
        }

        auto write_node_config = [&]( unsigned int i, const char *node_type )
        {
            const std::string base_write_path = binary_path + "/bridge_race_node" + std::to_string( i + 1u ) + "/";
            s_configs[i].Addr             = kDevPayoutAddr;
            s_configs[i].Cut              = kDevCutFraction;
            s_configs[i].TokenValueInGNUS = kDevTokenValue;
            s_configs[i].TokenID          = sgns::TokenID::FromBytes( { 0x00 } );
            s_configs[i].BaseWritePath    = base_write_path;

            const unsigned int port = kNodePortBase + i;
            WriteRaceNetworkConfig( base_write_path, port );
            sgns::GeniusNode::WriteSgnsConfig( base_write_path, node_type, /*is_processor=*/false );
            WriteBridgeChainsConfig( base_write_path );
        };

        write_node_config( 0u, "Full" );
        for ( unsigned int i = 1u; i < kNodeCount; ++i ) write_node_config( i, "Light" );

        std::array<std::string, kNodeCount> validator_addresses;
        for ( unsigned int i = 0u; i < kNodeCount; ++i )
        {
            const std::string key = DeriveNodeKey( i );
            auto preview = sgns::GeniusAccount::NewFromPrivateKey(
                s_configs[i].TokenID,
                key.c_str(),
                boost::filesystem::path( s_configs[i].BaseWritePath ),
                i == 0u );
            ASSERT_NE( preview, nullptr ) << "Failed to derive validator address " << i;
            validator_addresses[i] = preview->GetAddress();
        }

        sgns::Blockchain::SetAuthorizedFullNodeAddress( validator_addresses[0] );
        const std::vector<std::string> light_addresses(
            validator_addresses.begin() + 1, validator_addresses.end() );
        sgns::Blockchain::SetAdditionalGenesisValidatorAddresses( light_addresses );
        spdlog::info( "bridge_race: registered full authority and {} additional genesis validators",
                      kNodeCount - 1u );

        for ( unsigned int i = 0u; i < kNodeCount; ++i )
        {
            s_nodes[i] = GeniusNode::New( s_configs[i], sgns::FromPrivateKey{ DeriveNodeKey( i ) } );
            ASSERT_NE( s_nodes[i], nullptr ) << "Failed to create node index " << i;
            s_nodes[i]->SetChainlistFetcher( chainlist_fetcher );
            ASSERT_EQ( s_nodes[i]->GetAddress(), validator_addresses[i] )
                << "previewed validator address changed for node " << i;
        }

        // Star-topology PubSub mesh bootstrap: each Light node peers directly with the
        // Full node (sufficient for CRDT sync; a full 11x11 mesh is unnecessary).
        const std::string full_node_pubsub_addr = s_nodes[0]->GetPubSub()->GetLocalAddress();
        for ( unsigned int i = 1u; i < kNodeCount; ++i )
        {
            s_nodes[i]->GetPubSub()->AddPeers( { full_node_pubsub_addr } );
        }

        // All transaction managers must be READY before a TEST_F body attempts the
        // deliberately back-to-back ConfigureRpcEndpoint calls. The burn is still seeded
        // before endpoint configuration in each test, preserving the D-03 race trigger.
        ASSERT_WAIT_FOR_CONDITION(
            [&]()
            {
                return std::all_of(
                    s_nodes.begin(),
                    s_nodes.end(),
                    []( const std::shared_ptr<GeniusNode> &node )
                    { return node && node->GetState() == GeniusNode::NodeState::READY; } );
            },
            kRaceNodeReadyTimeout,
            "all 11 bridge-race nodes READY",
            nullptr );

        spdlog::info( "bridge_race: all {} nodes READY (RPC endpoints not yet configured)", kNodeCount );
    }

    /**
     * @brief Tears down the cluster, stops Anvil, and removes per-node data directories.
     */
    static void TearDownTestSuite()
    {
        spdlog::info( "bridge_race: tearing down nodes" );
        ClearConsensusObservers();

        // Shut down while the fixture still owns every node. This prevents a
        // watcher or mesh callback from becoming the last shared owner and
        // running GeniusNode destruction on its own worker. Tear down light
        // nodes first so the full-node hub remains available until every leaf
        // has stopped its watcher, transaction manager, consensus manager, and
        // CRDT services.
        for ( unsigned int offset = 0u; offset < kNodeCount; ++offset )
        {
            const unsigned int i = kNodeCount - 1u - offset;
            const auto start = std::chrono::steady_clock::now();
            spdlog::info( "bridge_race: teardown node={} phase=shutdown-start", i );
            sgns::BridgeRaceConsensusTestAccess::ShutdownNode( s_nodes[i] );
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start );
            spdlog::info( "bridge_race: teardown node={} phase=shutdown-complete elapsed_ms={}",
                          i,
                          elapsed.count() );
        }

        for ( unsigned int offset = 0u; offset < kNodeCount; ++offset )
        {
            const unsigned int i = kNodeCount - 1u - offset;
            const auto start = std::chrono::steady_clock::now();
            spdlog::info( "bridge_race: teardown node={} phase=destroy-start use_count={}",
                          i,
                          s_nodes[i] ? s_nodes[i].use_count() : 0 );
            s_nodes[i].reset();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start );
            spdlog::info( "bridge_race: teardown node={} phase=destroy-complete elapsed_ms={}",
                          i,
                          elapsed.count() );
        }
        spdlog::info( "bridge_race: teardown phase=nodes-complete" );
        s_anvil.Stop();
        spdlog::info( "bridge_race: teardown phase=anvil-complete" );
        std::error_code ec;
        for ( unsigned int i = 0u; i < kNodeCount; ++i )
        {
            std::filesystem::remove_all( s_configs[i].BaseWritePath, ec );
        }
    }
};

#endif // SUPERGENIUS_TEST_BRIDGE_RACE_FIXTURE_HPP
