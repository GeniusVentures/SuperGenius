// node_startup_test.cpp
//
// Node startup-timing tests. Verifies that both the genesis (full) node and a
// regular node reach the READY state quickly:
//   1. Genesis creator READY before the account-creation PubSub timeout.
//   2. Regular node READY shortly after the genesis node is ready.
//
// Both tests use bounded wait-conditions (no sleep_for) and report the measured
// latency so the startup budget can be tightened over time.

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <boost/dll.hpp>
#include <boost/json.hpp>

#include "account/AccountMessenger.hpp"
#include "account/GeniusAccount.hpp"
#include "account/GeniusNode.hpp"
#include "account/MintTransactionV2.hpp"
#include "base/hexutil.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "transaction/TransactionManager.hpp"
#include "crdt/globaldb/GlobalDbNetworkComposition.hpp"
#include "crypto/hasher.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "storage/database_error.hpp"
#include "testutil/local_trust_setup.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/wait_condition.hpp"
#include "trustedpeer/TrustStateStore.hpp"
#include "trustedpeer/TrustedPeerRegistry.hpp"

using namespace sgns;

namespace
{
    // Prefix for each node's working directory under the test-binary folder.
    constexpr const char *kNodeDirPrefix = "node_startup_";

    // Number of working directories swept clean in SetUp.
    constexpr int kNodeDirCleanupCount = 32;

    // Upper bound on how long a READY poll may wait before failing the test.
    constexpr int kReadyPollTimeoutMs = 30000;

    // 32-byte private key serialized as 64 hex characters.
    constexpr int kPrivateKeyHexLength = 64;

    // Size of the hex digit alphabet (0..15) used for deterministic key generation.
    constexpr int kHexAlphabetSize = 16;

    /// @brief Signs a bootstrap certificate against the real node's validator registry snapshot.
    outcome::result<ConsensusManager::Certificate> MakeStartupCertificate(
        const std::shared_ptr<GeniusAccount> &authority,
        const ConsensusManager::Subject      &subject,
        const std::string                    &registry_cid,
        const ValidatorRegistry::Registry    &registry )
    {
        const auto now_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );
        ConsensusManager::Proposal proposal;
        *proposal.mutable_subject() = subject;
        proposal.set_proposer_id( authority->GetAddress() );
        proposal.set_registry_cid( registry_cid );
        proposal.set_registry_epoch( registry.epoch() );
        proposal.set_timestamp( now_ms );
        // Proposal IDs hash the signing bytes with the ID cleared, before signing
        // the completed proposal through the same public API as consensus.
        auto id_copy = proposal;
        id_copy.clear_proposal_id();
        BOOST_OUTCOME_TRY( auto id_signing, ConsensusManager::ProposalSigningBytes( id_copy ) );
        const auto id_hash = crypto::sha2_256( id_signing.data(), id_signing.size() );
        proposal.set_proposal_id( base::hex_lower( gsl::span<const uint8_t>( id_hash.data(), id_hash.size() ) ) );
        BOOST_OUTCOME_TRY( auto proposal_signing, ConsensusManager::ProposalSigningBytes( proposal ) );
        const auto proposal_signature = authority->Sign( proposal_signing );
        proposal.set_signature( proposal_signature.data(), proposal_signature.size() );

        ConsensusManager::Vote vote;
        vote.set_proposal_id( proposal.proposal_id() );
        vote.set_voter_id( authority->GetAddress() );
        vote.set_approve( true );
        vote.set_timestamp( now_ms );
        BOOST_OUTCOME_TRY( auto vote_signing, ConsensusManager::VoteSigningBytes( vote ) );
        const auto vote_signature = authority->Sign( vote_signing );
        vote.set_signature( vote_signature.data(), vote_signature.size() );

        ConsensusManager::Certificate certificate;
        certificate.set_proposal_id( proposal.proposal_id() );
        certificate.set_registry_cid( registry_cid );
        certificate.set_registry_epoch( registry.epoch() );
        const auto total_weight = ValidatorRegistry::TotalWeight( registry );
        certificate.set_total_weight( total_weight );
        certificate.set_approved_weight( total_weight );
        certificate.set_timestamp( now_ms );
        *certificate.add_votes()        = vote;
        *certificate.mutable_proposal() = proposal;
        return certificate;
    }
} // namespace

class NodeStartupTest : public ::testing::Test
{
protected:
    struct NodeFixture
    {
        std::shared_ptr<GeniusNode>    node;
        std::shared_ptr<GeniusAccount> authority;
        std::filesystem::path          path;
        // Gossip relays the original publisher's peer ID, so nodes joining later
        // fetch this composition's roots from it. If it dies with the helper's
        // locals, those fetches burn the full graphsync connect-timeout on the
        // READY path. Declared last to be destroyed before the node.
        std::shared_ptr<sgns::crdt::GlobalDbNetworkComposition> composition;
    };

    static void SetUpTestSuite()
    {
        // Inject in-memory secure storage to avoid OS keychain prompts during tests.
        GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                                                { return std::make_shared<MemorySecureStorage>( identifier ); } );
    }

    // Create a node with a deterministic key derived from self_address. PubSub is
    // started synchronously in the constructor; callers wait for READY via a
    // wait-condition, so no fixed sleep is needed (a sleep would also obscure the
    // startup-timing measurements these tests make).
    NodeFixture CreateNode( const std::string                  &self_address,
                            const std::string                  &dev_addr,
                            const std::string                  &tokenValue,
                            TokenID                             tokenId,
                            bool                                isFullNode           = false,
                            std::optional<std::chrono::seconds> trust_sync_interval  = std::nullopt,
                            bool                                force_bootstrap_walk = false,
                            boost::json::object                 settings_overrides   = {} )
    {
        static std::atomic<int> nodeCounter{ 0 };
        int                     id = nodeCounter.fetch_add( 1 );

        std::string binaryPath = boost::dll::program_location().parent_path().string();
        auto        outPath    = binaryPath + "/" + kNodeDirPrefix + std::to_string( id ) + "/";

        GeniusNodeConfig devConfig = { dev_addr, "0.35", tokenValue, tokenId, outPath };

        std::hash<std::string>          hasher;
        size_t                          address_hash = hasher( self_address );
        std::mt19937                    rng( static_cast<uint32_t>( address_hash ) );
        std::uniform_int_distribution<> dist( 0, kHexAlphabetSize - 1 );
        std::string                     key;
        key.reserve( kPrivateKeyHexLength );
        std::generate_n( std::back_inserter( key ),
                         kPrivateKeyHexLength,
                         [&]()
                         {
                             static constexpr std::string_view hexChars = "0123456789abcdef";
                             return hexChars[dist( rng )];
                         } );

        GeniusNode::WriteNetworkConfig( devConfig.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
        if ( trust_sync_interval )
        {
            const auto        config_path = outPath + "network_config.json";
            std::ifstream     input( config_path );
            const std::string config_text( std::istreambuf_iterator<char>{ input }, {} );
            auto              config = boost::json::parse( config_text ).as_object();
            config.insert_or_assign( "trust_bootstrap_sync_interval_sec", trust_sync_interval->count() );
            input.close();
            std::ofstream output( config_path );
            output << boost::json::serialize( config );
        }
        auto authority = GeniusAccount::NewFromPrivateKey( tokenId, key.c_str(), outPath );
        EXPECT_TRUE( authority );
        if ( !authority )
        {
            return {};
        }
        sgns::test::WriteTrustedSgnsConfig( outPath,
                                            isFullNode ? "Full" : "Light",
                                            /*is_processor=*/false,
                                            /*rpc_catchup=*/false,
                                            { authority->GetAddress() },
                                            authority->GetAddress(),
                                            1,
                                            1,
                                            144 );
        if ( force_bootstrap_walk )
        {
            settings_overrides.insert_or_assign( "force_bootstrap_walk", true );
        }
        if ( !settings_overrides.empty() )
        {
            const auto        config_path = outPath + "sgns_config.json";
            std::ifstream     input( config_path );
            const std::string config_text( std::istreambuf_iterator<char>{ input }, {} );
            auto              config = boost::json::parse( config_text ).as_object();
            for ( const auto &setting : settings_overrides )
            {
                config.insert_or_assign( setting.key(), setting.value() );
            }
            input.close();
            std::ofstream output( config_path );
            output << boost::json::serialize( config );
        }
        return { GeniusNode::New( devConfig, sgns::FromPrivateKey{ key.c_str() } ),
                 std::move( authority ),
                 outPath,
                 {} };
    }

    void SubmitReviewedTrustAndAwaitReady( NodeFixture              &fixture,
                                           std::chrono::milliseconds *ready_elapsed   = nullptr,
                                           int                        poll_timeout_ms = kReadyPollTimeoutMs )
    {
        ASSERT_TRUE( fixture.node );
        ASSERT_TRUE( fixture.authority );
        ASSERT_EQ( fixture.node->GetState(), GeniusNode::NodeState::WAITING_FOR_TRUST_GENESIS );

        const auto network_config = fixture.path / "reviewed-trust-network.json";
        const auto database_path  = fixture.path / "reviewed-trust-globaldb";
        std::filesystem::create_directories( database_path );
        sgns::test::WriteGlobalDbNetworkConfig( network_config.string(),
                                                fixture.node->GetPubSub()->GetInterfaceAddress() );

        const std::string topic( TransactionManager::GNUS_FULL_NODES_TOPIC );
        sgns::crdt::GlobalDbNetworkComposition::Config composition_config;
        composition_config.network_config_path = network_config.string();
        composition_config.database_path       = database_path.string();
        composition_config.listen_topic        = topic;
        composition_config.broadcast_topic     = topic;
        auto composition_result = sgns::crdt::GlobalDbNetworkComposition::Create( std::move( composition_config ) );
        ASSERT_TRUE( composition_result.has_value() ) << composition_result.error().message();
        auto composition = composition_result.value();
        ASSERT_TRUE( composition->Start().has_value() );
        fixture.composition = composition;

        auto secure_crdt = std::make_shared<sgns::securecrdt::SecureCrdt>( composition->db(), topic );
        auto store = sgns::trustedpeer::TrustStateStore::Open(
            ( fixture.path / "reviewed-trust-state" ).string(), 144 );
        ASSERT_TRUE( store.has_value() ) << store.error().message();

        sgns::trustedpeer::GenesisManifest manifest;
        manifest.network_id              = 144;
        manifest.bootstrapper_public_key = fixture.authority->GetAddress();
        manifest.peers                   = { fixture.authority->GetAddress() };
        manifest.membership_threshold    = 1;
        manifest.burn_threshold          = 1;
        const auto canonical             = manifest.Canonicalized();
        ASSERT_TRUE( canonical.has_value() );
        const auto manifest_bytes = canonical->CanonicalBytes();
        ASSERT_TRUE( manifest_bytes.has_value() );

        auto registry = sgns::trustedpeer::TrustedPeerRegistry::NewProduction(
            secure_crdt,
            store.value(),
            *canonical,
            fixture.authority->Sign( *manifest_bytes ),
            fixture.authority->GetAddress(),
            [authority = fixture.authority]( const std::vector<uint8_t> &bytes ) { return authority->Sign( bytes ); } );
        ASSERT_TRUE( registry.has_value() ) << registry.error().message();
        ASSERT_TRUE( secure_crdt->RegisterFilters() );
        auto submitted = registry.value()->SubmitReviewedGenesisApproval();
        ASSERT_TRUE( submitted.has_value() ) << submitted.error().message();

        test::assertWaitForCondition( [&] { return fixture.node->GetState() == GeniusNode::NodeState::READY; },
                                      std::chrono::milliseconds( poll_timeout_ms ),
                                      "reviewed trust and deterministic initial burn did not reach READY",
                                      ready_elapsed );
    }

    void SetUp() override
    {
        std::string binaryPath = boost::dll::program_location().parent_path().string();
        for ( int i = 0; i < kNodeDirCleanupCount; ++i )
        {
            auto            dir = binaryPath + "/" + kNodeDirPrefix + std::to_string( i ) + "/";
            std::error_code ec;
            sgns::test::removeAllWithRetry( dir, ec );
        }
    }
};

TEST_F( NodeStartupTest, BootstrapRecoverySettingsAreLoadedStrictlyFromSgnsConfig )
{
    struct ConfigCase
    {
        std::string         name;
        boost::json::object settings;
        bool                enabled{ false };
        std::size_t         max_blocks{ 10000 };
        std::size_t         max_retries{ 8 };
    };

    const auto                    largest_size = std::numeric_limits<std::size_t>::max();
    const auto                    largest_json = static_cast<uint64_t>( largest_size );
    const std::vector<ConfigCase> cases{
        { "missing", {} },
        { "explicit_false", { { "force_bootstrap_walk", false } } },
        { "explicit_true", { { "force_bootstrap_walk", true } }, true },
        { "wrong_flag_type", { { "force_bootstrap_walk", "true" } } },
        { "positive_limits",
          { { "bootstrap_walk_max_blocks", 17 }, { "bootstrap_walk_max_retries", 3 } },
          false,
          17U,
          3U },
        { "largest_limits",
          { { "bootstrap_walk_max_blocks", largest_json }, { "bootstrap_walk_max_retries", largest_json } },
          false,
          largest_size,
          largest_size },
        { "zero_blocks",
          { { "bootstrap_walk_max_blocks", 0 }, { "bootstrap_walk_max_retries", 4 } },
          false,
          10000U,
          4U },
        { "zero_retries",
          { { "bootstrap_walk_max_blocks", 12 }, { "bootstrap_walk_max_retries", 0 } },
          false,
          12U,
          8U },
        { "negative_blocks",
          { { "bootstrap_walk_max_blocks", -1 }, { "bootstrap_walk_max_retries", 5 } },
          false,
          10000U,
          5U },
        { "negative_retries",
          { { "bootstrap_walk_max_blocks", 12 }, { "bootstrap_walk_max_retries", -1 } },
          false,
          12U,
          8U },
        { "string_limits", { { "bootstrap_walk_max_blocks", "20" }, { "bootstrap_walk_max_retries", "2" } } },
        { "fractional_limits", { { "bootstrap_walk_max_blocks", 1.5 }, { "bootstrap_walk_max_retries", 0.5 } } },
        { "overflow_limits", { { "bootstrap_walk_max_blocks", 1.0e100 }, { "bootstrap_walk_max_retries", 1.0e100 } } },
    };
    for ( const auto &config_case : cases )
    {
        SCOPED_TRACE( config_case.name );
        auto fixture = CreateNode( "bootstrap_config_" + config_case.name,
                                   "0xcafe",
                                   "1.0",
                                   TokenID::FromBytes( { 0x00 } ),
                                   false,
                                   std::nullopt,
                                   false,
                                   config_case.settings );
        ASSERT_TRUE( fixture.node );
        NodeTeardownGuard teardown{ fixture.node };
        const auto        settings = GeniusNodeTestAccess::BootstrapWalkSettingsOf( fixture.node );
        EXPECT_EQ( settings.enabled, config_case.enabled );
        EXPECT_EQ( settings.max_blocks, config_case.max_blocks );
        EXPECT_EQ( settings.max_retries, config_case.max_retries );
    }
}

// Replicated certificates are filtered while the trust gate still prevents the
// transaction manager from being constructed. Their canonical keys must already
// decode correctly, and invalid bindings or signatures must still be rejected.
TEST_F( NodeStartupTest, CanonicalCertificatesAcceptedBeforeTransactionManager )
{
    // Other tests may have left this process-wide handler installed. Only the
    // real node startup path may restore it for this fresh-boot regression.
    ConsensusManager::UnregisterSlotKeyHandler( NONCE_SUBJECT_TYPE );
    auto fixture = CreateNode( "early_certificate_slots", "0xcafe", "1.0", TokenID::FromBytes( { 0x00 } ), true );
    ASSERT_TRUE( fixture.node );
    NodeTeardownGuard teardown{ fixture.node };
    Blockchain::SetAuthorizedFullNodeAddress( fixture.node->GetAddress() );
    ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition(
        [&] { return fixture.node->GetState() == GeniusNode::NodeState::WAITING_FOR_TRUST_GENESIS; },
        std::chrono::milliseconds( kReadyPollTimeoutMs ),
        "fresh full node did not reach the trust genesis gate" ) );
    ASSERT_TRUE( fixture.node->GetTransactionManager().has_error() );

    auto registry = GeniusNodeTestAccess::GetValidatorRegistry( fixture.node );
    auto node_db  = GeniusNodeTestAccess::TransactionDatabaseOf( fixture.node );
    ASSERT_TRUE( registry );
    ASSERT_TRUE( node_db );
    const auto snapshot = registry->LoadCurrentRegistry();
    ASSERT_TRUE( snapshot.has_value() ) << snapshot.error().message();
    const auto registry_cid = registry->GetRegistryCid();
    ASSERT_FALSE( registry_cid.empty() );

    const auto network_config = fixture.path / "certificate-source-network.json";
    const auto database_path  = fixture.path / "certificate-source-globaldb";
    std::filesystem::create_directories( database_path );
    test::WriteGlobalDbNetworkConfig( network_config.string() );
    const std::string                        full_topic( TransactionManager::GNUS_FULL_NODES_TOPIC );
    crdt::GlobalDbNetworkComposition::Config source_config;
    source_config.network_config_path = network_config.string();
    source_config.database_path       = database_path.string();
    source_config.listen_topic        = full_topic;
    source_config.broadcast_topic     = full_topic;
    auto source                       = crdt::GlobalDbNetworkComposition::Create( std::move( source_config ) );
    ASSERT_TRUE( source.has_value() ) << source.error().message();
    fixture.composition = source.value();
    ASSERT_TRUE( fixture.composition->Start().has_value() );
    auto source_db = fixture.composition->db();
    ASSERT_TRUE( source_db );

    constexpr uint64_t kNonce        = 11;
    const auto         nonce_subject = ConsensusManager::CreateNonceSubject( fixture.authority->GetAddress(),
                                                                     kNonce,
                                                                     std::string( 64, '1' ),
                                                                     EmbeddedTransaction{},
                                                                     std::nullopt,
                                                                     std::nullopt );
    ASSERT_TRUE( nonce_subject.has_value() );
    auto nonce_certificate = MakeStartupCertificate( fixture.authority,
                                                     nonce_subject.value(),
                                                     registry_cid,
                                                     snapshot.value() );
    ASSERT_TRUE( nonce_certificate.has_value() );

    SGTransaction::DAGStruct dag;
    dag.set_type( "mint-v2" );
    dag.set_source_addr( fixture.authority->GetAddress() );
    dag.set_nonce( 2 );
    dag.set_timestamp( static_cast<int64_t>( nonce_certificate.value().timestamp() ) );
    const auto burn_hash = base::Hash256::fromReadableString( std::string( 64, 'b' ) );
    ASSERT_TRUE( burn_hash.has_value() );
    const auto mint         = MintTransactionV2::New( 42,
                                              "source-chain",
                                              TokenID::FromBytes( { 0x00 } ),
                                              std::move( dag ),
                                                      { { burn_hash.value(), 0, {} } },
                                              fixture.authority->GetAddress() );
    const auto mint_subject = ConsensusManager::CreateNonceSubject( fixture.authority->GetAddress(),
                                                                    mint.GetNonce(),
                                                                    mint.GetHash(),
                                                                    mint.SerializeToEmbeddedTransaction(),
                                                                    std::nullopt,
                                                                    std::nullopt );
    ASSERT_TRUE( mint_subject.has_value() );
    auto mint_certificate = MakeStartupCertificate( fixture.authority,
                                                    mint_subject.value(),
                                                    registry_cid,
                                                    snapshot.value() );
    ASSERT_TRUE( mint_certificate.has_value() );

    const auto write_certificate =
        [&source_db, &full_topic]( const ConsensusManager::Certificate &certificate, const std::string &key )
    {
        SCOPED_TRACE( key );
        const auto   serialized = certificate.SerializeAsString();
        base::Buffer value;
        value.put( serialized );
        ASSERT_TRUE( source_db->PutConvergentImmutable( { key }, value, { full_topic } ).has_value() );
    };
    const std::string nonce_key = "/cert/" + fixture.authority->GetAddress() + ":11";
    const std::string mint_key  = "/cert/" + mint.GetSlotID();
    write_certificate( nonce_certificate.value(), nonce_key );
    write_certificate( mint_certificate.value(), mint_key );

    const auto invalid_subject = ConsensusManager::CreateNonceSubject( fixture.authority->GetAddress(),
                                                                       kNonce + 1,
                                                                       std::string( 64, '2' ),
                                                                       EmbeddedTransaction{},
                                                                       std::nullopt,
                                                                       std::nullopt );
    ASSERT_TRUE( invalid_subject.has_value() );
    auto invalid_certificate = MakeStartupCertificate( fixture.authority,
                                                       invalid_subject.value(),
                                                       registry_cid,
                                                       snapshot.value() );
    ASSERT_TRUE( invalid_certificate.has_value() );
    auto *invalid_vote      = invalid_certificate.value().mutable_votes( 0 );
    auto  invalid_signature = invalid_vote->signature();
    ASSERT_FALSE( invalid_signature.empty() );
    invalid_signature.front() = static_cast<char>( invalid_signature.front() ^ 1 );
    invalid_vote->set_signature( invalid_signature );
    const std::string invalid_key = "/cert/" + fixture.authority->GetAddress() + ":12";

    // Local writes bypass ingress filters. Replicate an external DAG instead,
    // with the negative cases and a marker in one delta. The marker proves the
    // node filtered both rejected entries, even if parents merge out of order.
    const crdt::HierarchicalKey tip_key( "/startup/certificate-filter-tip" );
    base::Buffer                tip_value;
    tip_value.put( "external certificate history complete" );
    base::Buffer wrong_slot_value;
    wrong_slot_value.put( nonce_certificate.value().SerializeAsString() );
    base::Buffer invalid_value;
    invalid_value.put( invalid_certificate.value().SerializeAsString() );
    ASSERT_TRUE( source_db
                     ->Put( std::vector<crdt::GlobalDB::DataPair>{ { { "/cert/wrong-slot" }, wrong_slot_value },
                                                                   { { invalid_key }, invalid_value },
                                                                   { tip_key, tip_value } },
                            { full_topic } )
                     .has_value() );
    fixture.node->AddPeer( fixture.composition->interface_address() );
    constexpr auto kHeadRepeatInterval = std::chrono::milliseconds( 100 );
    auto           last_broadcast      = std::chrono::steady_clock::now() - kHeadRepeatInterval;
    ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition(
        [&]
        {
            const auto now = std::chrono::steady_clock::now();
            if ( now - last_broadcast >= kHeadRepeatInterval )
            {
                EXPECT_TRUE( source_db->RequestHeadBroadcast( { full_topic } ).has_value() );
                last_broadcast = now;
            }
            return node_db->Get( tip_key ).has_value() && node_db->Get( { nonce_key } ).has_value() &&
                   node_db->Get( { mint_key } ).has_value();
        },
        std::chrono::milliseconds( kReadyPollTimeoutMs ),
        "external certificate history did not replicate before the transaction manager exists" ) );
    const auto assert_certificate_stored =
        [&node_db]( const std::string &key, const ConsensusManager::Certificate &certificate )
    {
        const auto stored = node_db->Get( { key } );
        ASSERT_TRUE( stored.has_value() ) << "valid certificate rejected at " << key;
        EXPECT_EQ( stored.value().toString(), certificate.SerializeAsString() );
    };
    assert_certificate_stored( nonce_key, nonce_certificate.value() );
    assert_certificate_stored( mint_key, mint_certificate.value() );
    const auto wrong_slot = node_db->Get( { "/cert/wrong-slot" } );
    ASSERT_TRUE( wrong_slot.has_error() );
    EXPECT_EQ( wrong_slot.error(), storage::DatabaseError::NOT_FOUND );
    const auto invalid_slot = node_db->Get( { invalid_key } );
    ASSERT_TRUE( invalid_slot.has_error() );
    EXPECT_EQ( invalid_slot.error(), storage::DatabaseError::NOT_FOUND );
    EXPECT_EQ( fixture.node->GetState(), GeniusNode::NodeState::WAITING_FOR_TRUST_GENESIS );
    EXPECT_TRUE( fixture.node->GetTransactionManager().has_error() );
}

TEST_F( NodeStartupTest, ForcedBootstrapWalkRunsBeforeBlockchainIsReady )
{
    auto fixture = CreateNode( "forced_bootstrap_walk",
                               "0xcafe",
                               "1.0",
                               TokenID::FromBytes( { 0x00 } ),
                               false,
                               std::nullopt,
                               true );
    ASSERT_TRUE( fixture.node );
    NodeTeardownGuard teardown{ fixture.node };
    ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition(
        [&]
        {
            const auto db = GeniusNodeTestAccess::TransactionDatabaseOf( fixture.node );
            return db && db->GetForcedWalkStatus().state == crdt::CrdtDatastore::ForcedWalkState::Completed;
        },
        std::chrono::milliseconds( kReadyPollTimeoutMs ),
        "opt-in forced walkthrough was not requested before genesis became available" ) );
    EXPECT_EQ( fixture.node->GetState(), GeniusNode::NodeState::INITIALIZING_BLOCKCHAIN );
    EXPECT_TRUE( fixture.node->GetTransactionManager().has_error() );
}

TEST_F( NodeStartupTest, ForcedBootstrapWalkRetriesLateValidatorAfterReady )
{
    auto fixture = CreateNode( "forced_bootstrap_after_ready",
                               "0xcafe",
                               "1.0",
                               TokenID::FromBytes( { 0x00 } ),
                               true,
                               std::chrono::seconds( 1 ),
                               true );
    ASSERT_TRUE( fixture.node );
    NodeTeardownGuard teardown{ fixture.node };
    Blockchain::SetAuthorizedFullNodeAddress( fixture.node->GetAddress() );
    ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition(
        [&] { return fixture.node->GetState() == GeniusNode::NodeState::WAITING_FOR_TRUST_GENESIS; },
        std::chrono::milliseconds( kReadyPollTimeoutMs ),
        "full node did not reach its reviewed trust checkpoint" ) );
    ASSERT_NO_FATAL_FAILURE( SubmitReviewedTrustAndAwaitReady( fixture ) );
    ASSERT_EQ( fixture.node->GetState(), GeniusNode::NodeState::READY );
    auto node_db = GeniusNodeTestAccess::TransactionDatabaseOf( fixture.node );
    ASSERT_TRUE( node_db );
    ASSERT_TRUE( fixture.composition );
    const std::string           pattern = "^/?startup/late-recovery$";
    const crdt::HierarchicalKey key( "/startup/late-recovery" );
    ASSERT_TRUE( node_db->RegisterElementFilter( pattern,
                                                 []( const crdt::CrdtDatastore::Element & )
                                                 { return crdt::CRDTDataFilter::ElementFilterResult::Reject(); } ) );
    base::Buffer value;
    value.put( "validated after READY" );
    const auto published = fixture.composition->db()->Put(
        key,
        value,
        { std::string( TransactionManager::GNUS_FULL_NODES_TOPIC ) } );
    ASSERT_TRUE( published.has_value() ) << published.error().message();
    const auto cid_string = published.value().toString();
    ASSERT_TRUE( cid_string.has_value() );
    ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition(
        [&]
        {
            const auto retained = node_db->HasLocalBlock( cid_string.value() );
            const auto seal     = node_db->GetRaw( base::Buffer{}.put( cid_string.value() + "sealed" ) );
            return retained.has_value() && retained.value() && seal.has_value() && node_db->Get( key ).has_error() &&
                   !node_db->GetCRDTDataStore()->IsRootCIDPendingOrActive( published.value() );
        },
        std::chrono::milliseconds( kReadyPollTimeoutMs ),
        "rejected remote element did not leave a retained sealed delta" ) );
    node_db->UnregisterElementFilter( pattern );
    ASSERT_TRUE( node_db
                     ->RequestForcedWalk(
                         { std::string( TransactionManager::GNUS_FULL_NODES_TOPIC ) },
                         []( const crdt::CrdtDatastore::Element & ) -> outcome::result<bool> { return true; },
                         1000U )
                     .has_value() );
    const auto is_deferred = [&]
    {
        const auto status = node_db->GetForcedWalkStatus();
        return status.state == crdt::CrdtDatastore::ForcedWalkState::Incomplete && status.deferred > 0 &&
               !status.limit_reached && node_db->Get( key ).has_error();
    };
    ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition( is_deferred,
                                                           std::chrono::milliseconds( kReadyPollTimeoutMs ),
                                                           "missing validator falsely completed recovery" ) );
    GeniusNodeTestAccess::RequestBootstrapWalk( fixture.node );
    ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition( is_deferred,
                                                           std::chrono::milliseconds( kReadyPollTimeoutMs ),
                                                           "production continuation skipped deferral" ) );
    EXPECT_EQ( fixture.node->GetState(), GeniusNode::NodeState::READY );
    ASSERT_TRUE( node_db->RegisterElementFilter( pattern,
                                                 []( const crdt::CrdtDatastore::Element & )
                                                 { return crdt::CRDTDataFilter::ElementFilterResult::Accept(); } ) );
    bool left_ready = false;
    ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition(
        [&]
        {
            left_ready           = left_ready || fixture.node->GetState() != GeniusNode::NodeState::READY;
            const auto recovered = node_db->Get( key );
            return recovered.has_value() && recovered.value() == value;
        },
        std::chrono::milliseconds( kReadyPollTimeoutMs ),
        "bootstrap recovery did not retry the late validator after READY" ) );
    EXPECT_FALSE( left_ready );
    EXPECT_EQ( fixture.node->GetState(), GeniusNode::NodeState::READY );
}

// Processing gossip is live before blockchain genesis and the trust controller.
// A shared head received then must already follow FullNode-only parent links;
// otherwise the head is sealed with incomplete ancestry before trust subscribes.
TEST_F( NodeStartupTest, ProcessingHeadFetchesFullNodeAncestryBeforeBlockchainGenesis )
{
    auto fixture = CreateNode( "early_full_node_ancestry", "0xcafe", "1.0", TokenID::FromBytes( { 0x00 } ) );
    ASSERT_TRUE( fixture.node );
    NodeTeardownGuard teardown{ fixture.node };
    ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition(
        [&] { return fixture.node->GetState() == GeniusNode::NodeState::INITIALIZING_BLOCKCHAIN; },
        std::chrono::milliseconds( kReadyPollTimeoutMs ),
        "fresh light node did not wait for blockchain genesis" ) );
    ASSERT_TRUE( fixture.node->GetTransactionManager().has_error() );

    auto       node_db          = GeniusNodeTestAccess::TransactionDatabaseOf( fixture.node );
    const auto processing_topic = GeniusNodeTestAccess::ProcessingTopicOf( fixture.node );
    const auto full_topic       = std::string( TransactionManager::GNUS_FULL_NODES_TOPIC );
    ASSERT_TRUE( node_db );
    ASSERT_FALSE( processing_topic.empty() );
    EXPECT_EQ( node_db->GetForcedWalkStatus().state, crdt::CrdtDatastore::ForcedWalkState::Idle );

    const auto network_config = fixture.path / "early-history-network.json";
    const auto database_path  = fixture.path / "early-history-globaldb";
    std::filesystem::create_directories( database_path );
    sgns::test::WriteGlobalDbNetworkConfig( network_config.string() );
    sgns::crdt::GlobalDbNetworkComposition::Config source_config;
    source_config.network_config_path = network_config.string();
    source_config.database_path       = database_path.string();
    source_config.listen_topic        = processing_topic;
    // FullNode-only writes must not be announced on the processing topic.
    source_config.broadcast_topic = full_topic;
    auto source                   = sgns::crdt::GlobalDbNetworkComposition::Create( std::move( source_config ) );
    ASSERT_TRUE( source.has_value() ) << source.error().message();
    fixture.composition = source.value();
    ASSERT_TRUE( fixture.composition->Start().has_value() );
    auto source_db = fixture.composition->db();
    ASSERT_TRUE( source_db );
    source_db->AddTopicName( full_topic );

    const crdt::HierarchicalKey parent_key( "/startup/full-node-only-ancestor" );
    const crdt::HierarchicalKey tip_key( "/startup/shared-processing-tip" );
    base::Buffer                parent_value;
    base::Buffer                tip_value;
    parent_value.put( "full-node history before trust startup" );
    tip_value.put( "shared processing and full-node head" );
    const auto parent = source_db->Put( parent_key, parent_value, { full_topic } );
    ASSERT_TRUE( parent.has_value() ) << parent.error().message();
    const auto tip = source_db->Put( tip_key, tip_value, { processing_topic, full_topic } );
    ASSERT_TRUE( tip.has_value() ) << tip.error().message();
    const auto parent_cid = parent.value().toString();
    ASSERT_TRUE( parent_cid.has_value() );

    // Connect only after both writes: the node receives the shared tip and must
    // discover its parent through the FullNode link, rather than a parent gossip.
    fixture.node->AddPeer( fixture.composition->interface_address() );
    constexpr auto kHeadRepeatInterval = std::chrono::milliseconds( 100 );
    auto           last_broadcast      = std::chrono::steady_clock::now() - kHeadRepeatInterval;
    ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition(
        [&]
        {
            const auto now = std::chrono::steady_clock::now();
            if ( now - last_broadcast >= kHeadRepeatInterval )
            {
                EXPECT_TRUE( source_db->RequestHeadBroadcast( { processing_topic } ).has_value() );
                last_broadcast = now;
            }
            const auto replicated_tip    = node_db->Get( tip_key );
            const auto replicated_parent = node_db->Get( parent_key );
            return replicated_tip.has_value() && replicated_parent.has_value();
        },
        std::chrono::milliseconds( kReadyPollTimeoutMs ),
        "processing head arrived without fetching its FullNode-only ancestry before trust startup" ) );

    const auto replicated_parent = node_db->Get( parent_key );
    ASSERT_TRUE( replicated_parent.has_value() );
    EXPECT_EQ( replicated_parent.value().toString(), parent_value.toString() );
    const auto parent_present = node_db->HasLocalBlock( parent_cid.value() );
    ASSERT_TRUE( parent_present.has_value() );
    EXPECT_TRUE( parent_present.value() );
    EXPECT_EQ( fixture.node->GetState(), GeniusNode::NodeState::INITIALIZING_BLOCKCHAIN );
    EXPECT_TRUE( fixture.node->GetTransactionManager().has_error() );
}

// A node can enter the trust gate before it has any request-topic peers. Once
// a peer arrives it must request existing trust heads repeatedly, although the
// transaction manager (which normally performs head synchronization) is absent.
TEST_F( NodeStartupTest, TrustBootstrapRequestsExistingHeadsAfterPeerArrives )
{
    constexpr auto kTrustSyncInterval = std::chrono::seconds( 1 );
    auto           fixture            = CreateNode( "trust_bootstrap_late_peer",
                               "0xcafe",
                               "1.0",
                               TokenID::FromBytes( { 0x00 } ),
                               true,
                               kTrustSyncInterval );
    ASSERT_TRUE( fixture.node );
    NodeTeardownGuard teardown{ fixture.node };
    Blockchain::SetAuthorizedFullNodeAddress( fixture.node->GetAddress() );
    ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition(
        [&] { return fixture.node->GetState() == GeniusNode::NodeState::WAITING_FOR_TRUST_GENESIS; },
        std::chrono::milliseconds( kReadyPollTimeoutMs ),
        "offline node did not reach the trust genesis gate" ) );
    ASSERT_TRUE( fixture.node->GetTransactionManager().has_error() );

    auto       observer_pubsub = std::make_shared<ipfs_pubsub::GossipPubSub>();
    const auto start_error     = observer_pubsub->Start( 0, {}, "0.0.0.0", { "127.0.0.1" } ).get();
    ASSERT_FALSE( start_error ) << start_error.message();

    AccountMessenger::InterfaceMethods methods;
    methods.get_local_nonce_ = []( const std::string & ) -> outcome::result<uint64_t> { return uint64_t{ 0 }; };
    methods.sign_ =
        [authority = fixture.authority]( std::vector<uint8_t> data ) -> outcome::result<std::vector<uint8_t>>
    { return authority->Sign( data ); };
    methods.verify_signature_ = []( const std::string   &address,
                                    const std::string   &signature,
                                    std::vector<uint8_t> data ) -> outcome::result<bool>
    { return GeniusAccount::VerifySignature( address, std::string_view( signature ), data ); };
    auto observer = AccountMessenger::New( fixture.authority->GetAddress(), observer_pubsub, std::move( methods ) );
    ASSERT_TRUE( observer );

    auto full_head_requests     = std::make_shared<std::atomic<unsigned>>( 0 );
    auto last_full_head_request = std::make_shared<std::atomic<std::chrono::steady_clock::time_point>>(
        std::chrono::steady_clock::now() );
    observer->RegisterHeadRequestHandler(
        [full_head_requests, last_full_head_request]( const std::set<std::string> &topics )
        {
            if ( topics.count( std::string( TransactionManager::GNUS_FULL_NODES_TOPIC ) ) != 0 )
            {
                full_head_requests->fetch_add( 1 );
                last_full_head_request->store( std::chrono::steady_clock::now() );
            }
        } );

    // Dial only after WAITING, so the first request has no mesh to deliver it.
    fixture.node->AddPeer( observer_pubsub->GetInterfaceAddress() );
    ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition(
        [&] { return full_head_requests->load() >= 2; },
        std::chrono::milliseconds( kReadyPollTimeoutMs ),
        "trust genesis gate did not retry signed full-node head requests after a peer arrived" ) );
    EXPECT_EQ( fixture.node->GetState(), GeniusNode::NodeState::WAITING_FOR_TRUST_GENESIS );
    EXPECT_TRUE( fixture.node->GetTransactionManager().has_error() );

    ASSERT_NO_FATAL_FAILURE(
        test::MakeNodeReadyWithLocalTrust( fixture.node, std::chrono::milliseconds( kReadyPollTimeoutMs ) ) );

    // Allow an already published request to drain, then require a quiet window
    // longer than the retry interval. TransactionManager's ordinary head sync
    // begins at 30 seconds, beyond this bounded check.
    last_full_head_request->store( std::chrono::steady_clock::now() );
    ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition(
        [&] { return std::chrono::steady_clock::now() - last_full_head_request->load() >= 2 * kTrustSyncInterval; },
        5 * kTrustSyncInterval,
        "trust bootstrap continued requesting heads after the node reached READY" ) );
    GeniusNodeTestAccess::StopNode( fixture.node );
    observer->Stop();
    observer_pubsub->Stop();
}

// ---------------------------------------------------------------------------
// 1. GENESIS NODE STARTUP
// ---------------------------------------------------------------------------

// Reproduces the genesis-creator startup stall: the authorized full node creates
// its own account-creation block, so reaching READY must NOT be gated by the
// RequestAccountCreation PubSub timeout (TIMEOUT_ACC_CREATION_BLOCK_MS = 8000ms).
//
// Bug under test: Blockchain::GenesisReceivedCallback issued
// RequestAccountCreation(8000) and, with no peers, waited the full timeout
// before falling back to CreateAccountCreationBlock — adding ~8s to genesis-node
// startup. The budget below is intentionally under the 8s PubSub timeout, so the
// test fails while that timeout gates startup and passes once the genesis creator
// creates its account-creation block directly.
TEST_F( NodeStartupTest, GenesisCreatorReadyBeforeAccountCreationPubsubTimeout )
{
    std::cout << "=== Starting Genesis Creator Ready Before Account-Creation Pubsub Timeout Test ===" << std::endl;

    // Must stay below TIMEOUT_ACC_CREATION_BLOCK_MS (8000ms) to detect the
    // PubSub-timeout stall. The stall would park the node in
    // INITIALIZING_BLOCKCHAIN for ~8s, so the budget only covers
    // INITIALIZING_BLOCKCHAIN -> READY. INITIALIZING_DATABASE (RocksDB open)
    // can legitimately stall for tens of seconds when parallel test binaries
    // contend on disk, and must not count against the budget.
    constexpr int kGenesisCreatorReadyBudgetMs = 7000;

    auto node_full = CreateNode( "full_node_acc_creation_timing",
                                 "0xcafe",
                                 "1.0",
                                 TokenID::FromBytes( { 0x00 } ),
                                 true );
    ASSERT_TRUE( node_full.node );
    Blockchain::SetAuthorizedFullNodeAddress( node_full.node->GetAddress() );

    std::chrono::milliseconds restricted_elapsed_ms;
    test::assertWaitForCondition( [&]() {
                                      return node_full.node->GetState() ==
                                             GeniusNode::NodeState::WAITING_FOR_TRUST_GENESIS;
                                  },
                                  std::chrono::milliseconds( kReadyPollTimeoutMs ),
                                  "genesis creator never reached the restricted first-boot checkpoint",
                                  &restricted_elapsed_ms );

    std::cout << "Genesis creator reached restricted first boot in " << restricted_elapsed_ms.count() << "ms"
              << std::endl;

    ASSERT_LT( restricted_elapsed_ms.count(), kGenesisCreatorReadyBudgetMs )
        << "Genesis creator restricted first boot took " << restricted_elapsed_ms.count()
        << "ms — startup is gated by the RequestAccountCreation PubSub timeout. "
        << "The genesis creator should create its own account-creation block directly "
        << "instead of waiting on a PubSub response that never arrives (no peers).";

    SubmitReviewedTrustAndAwaitReady( node_full );

    std::cout << "=== Genesis Creator Ready Before Account-Creation Pubsub Timeout Test Completed ===" << std::endl;
}

// ---------------------------------------------------------------------------
// 2. REGULAR NODE STARTUP (after genesis is ready)
// ---------------------------------------------------------------------------

// Verifies a regular (non-full) node reaches READY quickly once the genesis node
// is already up. The genesis node is brought to READY first, then the regular
// node is created, connected, and its READY latency is measured.
TEST_F( NodeStartupTest, RegularNodeReadyQuicklyAfterGenesisReady )
{
    std::cout << "=== Starting Regular Node Ready Quickly After Genesis Ready Test ===" << std::endl;

    // Bring the genesis full node to READY first.
    auto genesisNode = CreateNode( "genesis_node_startup", "0xcafe", "1.0", TokenID::FromBytes( { 0x00 } ), true );
    ASSERT_TRUE( genesisNode.node );
    Blockchain::SetAuthorizedFullNodeAddress( genesisNode.node->GetAddress() );

    std::chrono::milliseconds genesis_restricted_ms;
    test::assertWaitForCondition( [&]() {
                                      return genesisNode.node->GetState() ==
                                             GeniusNode::NodeState::WAITING_FOR_TRUST_GENESIS;
                                  },
                                  std::chrono::milliseconds( kReadyPollTimeoutMs ),
                                  "genesis node never reached restricted first boot",
                                  &genesis_restricted_ms );
    SubmitReviewedTrustAndAwaitReady( genesisNode );
    std::cout << "Genesis node reached reviewed READY after restricted first boot" << std::endl;

    // Create a regular node and connect it to the genesis node.
    auto regularNode = CreateNode( "regular_node_startup", "0xcafe", "1.0", TokenID::FromBytes( { 0x00 } ), false );
    // GetInterfaceAddress() returns the full multiaddr WITH peer ID (required for
    // GeniusNode::AddPeer to dial). GetLocalAddress() omits the peer ID, so the dial never
    // completes and the regular node can't reach the genesis node — the cause of
    // the 5s nonce-timeout stall. This matches every other multi-node E2E test.
    ASSERT_TRUE( regularNode.node );
    regularNode.node->AddPeer( genesisNode.node->GetPubSub()->GetInterfaceAddress() );

    // Measure the regular node's READY latency (connect -> READY). READY is
    // gated on the trust lifecycle, which is replication-bound: the node must
    // fetch the reviewed-genesis approval CID over graphsync and activate the
    // initial-burn quorum before it may report READY. A quorum cannot skip
    // those round trips, so the budget reflects the platform's replication
    // floor — Windows CI runners need an order of magnitude more than the
    // 10s the other platforms hold.
#ifdef _WIN32
    constexpr int kRegularReadyBudgetMs   = 60000;
    constexpr int kRegularReadyPollTimeMs = 90000;
#else
    constexpr int kRegularReadyBudgetMs   = 10000;
    constexpr int kRegularReadyPollTimeMs = kReadyPollTimeoutMs;
#endif

    std::chrono::milliseconds regular_restricted_ms;
    test::assertWaitForCondition( [&]() {
                                      return regularNode.node->GetState() ==
                                             GeniusNode::NodeState::WAITING_FOR_TRUST_GENESIS;
                                  },
                                  std::chrono::milliseconds( kRegularReadyPollTimeMs ),
                                  "regular node never reached restricted first boot after genesis was ready",
                                  &regular_restricted_ms );
    std::chrono::milliseconds regular_ready_ms;
    SubmitReviewedTrustAndAwaitReady( regularNode, &regular_ready_ms, kRegularReadyPollTimeMs );
    const auto regular_total_ms = regular_restricted_ms + regular_ready_ms;
    std::cout << "Regular node reached reviewed READY in " << regular_total_ms.count()
              << "ms (after genesis was ready)" << std::endl;

    ASSERT_LT( regular_total_ms.count(), kRegularReadyBudgetMs )
        << "Regular node reviewed READY took " << regular_total_ms.count()
        << "ms after the genesis node was ready — startup latency is too high.";

    std::cout << "=== Regular Node Ready Quickly After Genesis Ready Test Completed ===" << std::endl;
}

// ---------------------------------------------------------------------------
// 3. DEFAULT BURN RATE END-TO-END REGRESSION (BURN-03)
// ---------------------------------------------------------------------------

// Exercises the real INITIALIZING_TRANSACTIONS construction path end-to-end
// (SecureCrdt -> TrustedPeerRegistry -> BurnConfig -> TransactionManager, wired
// in Phase 11 Plan 02) via an actual running GeniusNode reaching READY, and
// confirms a freshly-seeded genesis node's default burn rate is unchanged: 1%
// (100 basis points out of 10000), matching pre-milestone behavior.
TEST_F( NodeStartupTest, GenesisNodeDefaultBurnRateIsOnePercent )
{
    std::cout << "=== Starting Genesis Node Default Burn Rate Is One Percent Test ===" << std::endl;

    auto node_full = CreateNode( "full_node_burn_rate_default",
                                 "0xcafe",
                                 "1.0",
                                 TokenID::FromBytes( { 0x00 } ),
                                 true );
    ASSERT_TRUE( node_full.node );
    Blockchain::SetAuthorizedFullNodeAddress( node_full.node->GetAddress() );

    test::assertWaitForCondition( [&]() {
                                      return node_full.node->GetState() ==
                                             GeniusNode::NodeState::WAITING_FOR_TRUST_GENESIS;
                                  },
                                  std::chrono::milliseconds( kReadyPollTimeoutMs ),
                                  "genesis node never reached restricted first boot" );
    SubmitReviewedTrustAndAwaitReady( node_full );

    ASSERT_EQ( sgns::GeniusNode::GetBurnBasisPoints(), 100u )
        << "Genesis node's default burn rate must remain 1% (100 basis points) until a "
        << "quorum-signed BurnConfig update changes it (BURN-03 regression).";
    ASSERT_EQ( sgns::GeniusNode::GetBasisPointsTotal(), 10000u );

    std::cout << "=== Genesis Node Default Burn Rate Is One Percent Test Completed ===" << std::endl;
}
