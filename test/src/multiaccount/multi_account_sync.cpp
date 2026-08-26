#include <chrono>
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <iostream>
#include <cstdint>
#include <cstdio>
#include <system_error>

#ifdef _WIN32
//#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#include <functional>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <random>
#include <ctime>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <optional>

#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/asio.hpp>
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "account/GeniusAccount.hpp"
#include "account/BurnConfig.hpp"
#include "account/GeniusNode.hpp"
#include "account/TransactionManager.hpp"
#include "account/TrustStartupController.hpp"
#include "crdt/globaldb/GlobalDbNetworkComposition.hpp"
#include "FileManager.hpp"
#include <boost/dll.hpp>
#include <boost/algorithm/string/replace.hpp>
#include "testutil/mint_source_hash.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/TestMintInputValidator.hpp"
#include "testutil/wait_condition.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "trustedpeer/TrustStateStore.hpp"
#include "trustedpeer/TrustedPeerRegistry.hpp"

using namespace sgns;

namespace sgns
{
    class MultiAccountTestAccess
    {
    public:
        struct AccountGenerationSnapshot
        {
            std::shared_ptr<GeniusAccount>      account;
            std::shared_ptr<TransactionManager> manager;
            std::string                         account_address;
            std::string                         manager_address;
            uint64_t                            generation = 0;
            uint64_t                            catchup_generation = 0;
        };

        static AccountGenerationSnapshot SnapshotAccountGeneration( const std::shared_ptr<GeniusNode> &node )
        {
            if ( !node ) return {};
            auto snapshot = node->SnapshotAccountServices();
            return { snapshot.account,
                     snapshot.manager,
                     snapshot.account ? snapshot.account->GetAddress() : std::string{},
                     snapshot.manager && snapshot.manager->account_m ? snapshot.manager->account_m->GetAddress()
                                                                     : std::string{},
                     snapshot.generation,
                     snapshot.catchup_generation };
        }

        static uint64_t InjectCatchupCallback( const std::shared_ptr<GeniusNode> &node,
                                               const AccountGenerationSnapshot  &snapshot,
                                               std::atomic_uint64_t              &side_effects )
        {
            if ( !node ) return 0;
            GeniusNode::AccountServiceSnapshot captured{ snapshot.account, snapshot.manager, snapshot.generation };
            node->ApplyIfCurrentAccountServices( captured, [&] { ++side_effects; } );
            return node->catchup_callback_owner_generation_.load();
        }

        static std::shared_ptr<ValidatorRegistry> GetValidatorRegistry( const std::shared_ptr<GeniusNode> &node )
        {
            return node && node->blockchain_ ? node->blockchain_->GetValidatorRegistry() : nullptr;
        }

        static bool ConfigureConsensus( const std::shared_ptr<GeniusNode> &node,
                                        size_t                             certificates_per_batch,
                                        std::chrono::milliseconds          certificate_delay )
        {
            if ( !node || !node->blockchain_ || !node->blockchain_->consensus_manager_ )
            {
                return false;
            }

            auto registry = node->blockchain_->GetValidatorRegistry();
            if ( !registry )
            {
                return false;
            }

            registry->SetCertificatesPerBatch( certificates_per_batch );
            node->blockchain_->consensus_manager_->ConfigureCertificateDelay( certificate_delay );
            return true;
        }

        /// Fetches a finalized consensus certificate by subject hash, so a test can inspect who voted.
        /// Query this on a node that definitely holds the certificate (e.g. the full node) — an
        /// abstaining node's own view says nothing about what it did or did not sign.
        static std::optional<ConsensusCertificate> GetCertificate( const std::shared_ptr<GeniusNode> &node,
                                                                   const std::string                 &subject_hash )
        {
            if ( !node || !node->blockchain_ || !node->blockchain_->consensus_manager_ )
            {
                return std::nullopt;
            }
            auto result = node->blockchain_->consensus_manager_->GetCertificateBySubjectHash( subject_hash );
            if ( result.has_error() )
            {
                return std::nullopt;
            }
            return result.value();
        }

        static inline std::string GetDatabasePath( const std::shared_ptr<GeniusNode> &node )
        {
            return node ? node->write_base_path_ + node->gnus_network_full_path_ : std::string{};
        }

        static bool RemoveRegistryPersistence( const std::string   &database_path,
                                               std::vector<uint8_t> registry_block_key )
        {
            auto datastore_result = storage::rocksdb::create( database_path );
            if ( datastore_result.has_error() )
            {
                return false;
            }
            auto datastore = std::move( datastore_result.value() );

            base::Buffer registry_cid_key;
            registry_cid_key.put( std::string( ValidatorRegistry::RegistryCidKey() ) );
            if ( datastore->remove( registry_cid_key ).has_error() )
            {
                return false;
            }

            const base::Buffer block_key( std::move( registry_block_key ) );
            return datastore->remove( block_key ).has_value() && !datastore->contains( block_key );
        }

        static std::shared_ptr<TransactionManager> GetTransactionManager( const std::shared_ptr<GeniusNode> &node )
        {
            return node ? node->transaction_manager_ : nullptr;
        }

        static uint64_t GetTransactionManagerConstructionCount( const std::shared_ptr<GeniusNode> &node )
        {
            return node ? node->transaction_manager_construction_count_.load() : 0;
        }

        static uint64_t GetTransactionManagerStartCount( const std::shared_ptr<GeniusNode> &node )
        {
            return node ? node->transaction_manager_start_count_.load() : 0;
        }

        static uint64_t GetTransactionManagerOwnerGeneration( const std::shared_ptr<GeniusNode> &node )
        {
            return node ? node->transaction_manager_owner_generation_.load() : 0;
        }

        static uint64_t GetAccountTransactionCallbackOwnerGeneration( const std::shared_ptr<GeniusNode> &node )
        {
            return node ? node->account_transaction_callback_owner_generation_.load() : 0;
        }

        static uint64_t GetBlockchainSlotHashOwnerGeneration( const std::shared_ptr<GeniusNode> &node )
        {
            return node ? node->blockchain_slot_hash_owner_generation_.load() : 0;
        }

        static outcome::result<std::string> ResolveAccountTransactionCid( const std::shared_ptr<GeniusNode> &node,
                                                                          const std::string                &tx_hash )
        {
            if ( !node || !node->transaction_manager_ )
            {
                return outcome::failure( std::errc::owner_dead );
            }
            return node->transaction_manager_->GetTransactionCID( tx_hash );
        }

        static std::shared_ptr<const sgns::account::ConfirmedBurnValueProvider> GetManagerBurnProvider(
            const std::shared_ptr<GeniusNode> &node )
        {
            return node && node->transaction_manager_ ? node->transaction_manager_->confirmed_burn_provider_ : nullptr;
        }

        static std::shared_ptr<const sgns::account::ConfirmedBurnValueProvider> GetNodeBurnProvider(
            const std::shared_ptr<GeniusNode> &node )
        {
            return node && node->burn_config_ ? node->burn_config_->GetConfirmedValueProvider() : nullptr;
        }

    };

} // namespace sgns

class MultiAccountTest : public ::testing::Test
{
protected:
    static constexpr std::string_view FILE_PREFIX = "mat_";

    static std::string DeterministicKey( const std::string &self_address )
    {
        std::hash<std::string>          hasher;
        std::mt19937                    rng( static_cast<uint32_t>( hasher( self_address ) ) );
        std::uniform_int_distribution<> dist( 0, 15 );
        std::string                     key;
        key.reserve( 64 );
        std::generate_n( std::back_inserter( key ), 64, [&]() {
            static constexpr std::string_view hexChars = "0123456789abcdef";
            return hexChars[dist( rng )];
        } );
        return key;
    }

    void AddCanonicalTrustPeer( const std::string &self_address )
    {
        const auto path = boost::dll::program_location().parent_path() / "mat_trust_authorities";
        auto account = GeniusAccount::NewFromPrivateKey(
            TokenID::FromBytes( { 0x00 } ), DeterministicKey( self_address ).c_str(), path );
        ASSERT_TRUE( account );
        trust_peers_.push_back( std::move( account ) );
    }

    /// @param nodeTypeOverride Writes this literal role into sgns_config.json instead of the
    ///        Full/Light implied by @p isFullNode. Trailing and defaulted so the ~15 existing
    ///        call sites are untouched; used to spin up an "Archive" node.
    std::shared_ptr<sgns::GeniusNode> CreateNode( const std::string         &self_address,
                                                  bool                       isFullNode          = false,
                                                  bool                       isProcessor         = false,
                                                  bool                       isGenesisAuthorized = false,
                                                  std::string                existingBasePath    = {},
                                                  bool                       rpcCatchup          = false,
                                                  std::optional<std::string> nodeTypeOverride    = std::nullopt )
    {
        static std::atomic<int> nodeCounter{ 0 };
        const bool              reuseStorage = !existingBasePath.empty();
        std::string             outPathStr   = std::move( existingBasePath );
        if ( outPathStr.empty() )
        {
            const auto id         = nodeCounter.fetch_add( 1 );
            const auto binaryPath = boost::dll::program_location().parent_path();
            const auto outPath    = binaryPath / ( std::string( FILE_PREFIX ) + std::to_string( id ) );
            outPathStr            = outPath.generic_string() + '/';
        }
        else if ( outPathStr.back() != '/' )
        {
            outPathStr.push_back( '/' );
        }

        GeniusNodeConfig devConfig = { "0xcafe", "0.35", "1.0", TokenID::FromBytes( { 0x00 } ), outPathStr };

        if ( !reuseStorage )
        {
            sgns::test::removeAllWithRetry( devConfig.BaseWritePath );
            std::filesystem::create_directories( devConfig.BaseWritePath );
            {
                std::ofstream bridgeConfigFile( devConfig.BaseWritePath + "bridge_chains_config.json" );
                bridgeConfigFile << "{}";
            }
        }

        const auto key = DeterministicKey( self_address );

        if ( !reuseStorage )
        {
            sgns::GeniusNode::WriteNetworkConfig( devConfig.BaseWritePath, 0, /*auto_dht=*/false );
        }
        auto authority = GeniusAccount::NewFromPrivateKey(
            devConfig.TokenID, key.c_str(), devConfig.BaseWritePath );
        if ( !authority )
        {
            return nullptr;
        }
        if ( isGenesisAuthorized )
        {
            trust_authority_ = authority;
        }
        const auto configured_authority = trust_authority_ ? trust_authority_ : authority;
        if ( !reuseStorage )
        {
            std::ofstream config( devConfig.BaseWritePath + "sgns_config.json" );
            config << "{\"net_id\":144,\"subnet_id\":144,\"node_type\":\""
                   << nodeTypeOverride.value_or( isFullNode ? "Full" : "Light" )
                   << "\",\"is_processor\":" << ( isProcessor ? "true" : "false" )
                   << ",\"rpc_catchup\":" << ( rpcCatchup ? "true" : "false" )
                   << ",\"trusted_peers\":[\"" << configured_authority->GetAddress();
            for ( const auto &peer : trust_peers_ )
            {
                config << "\",\"" << peer->GetAddress();
            }
            config << "\"],\"bootstrapper_node\":\"" << configured_authority->GetAddress()
                   << "\",\"trusted_peer_quorum_threshold\":" << ( ( trust_peers_.size() + 1 ) / 2 + 1 )
                   << ",\"burn_config_quorum_threshold\":" << ( trust_peers_.size() + 1 - ( trust_peers_.size() + 1 ) / 3 )
                   << "}";
        }
        auto node = sgns::GeniusNode::New( devConfig, sgns::FromPrivateKey{ key } );
        if ( isGenesisAuthorized )
        {
            sgns::Blockchain::SetAuthorizedFullNodeAddress( node->GetAddress() );
        }

        node_base_paths_.insert_or_assign( node.get(), devConfig.BaseWritePath );
        node_authorities_.insert_or_assign( node.get(), configured_authority );
        return node;
    }

    const std::string &GetBaseWritePath( const std::shared_ptr<GeniusNode> &node ) const
    {
        return node_base_paths_.at( node.get() );
    }

    void WaitForReady( const std::shared_ptr<GeniusNode> &node )
    {
        sgns::test::assertWaitForCondition(
            [&]()
            {
                return node->GetState() == GeniusNode::NodeState::WAITING_FOR_TRUST_GENESIS ||
                       node->GetState() == GeniusNode::NodeState::WAITING_FOR_BURN_GENESIS ||
                       node->GetState() == GeniusNode::NodeState::READY;
            },
            std::chrono::milliseconds( 50000 ),
            "node did not reach a trust lifecycle checkpoint: " + node->GetAddress() );

        if ( node->GetState() != GeniusNode::NodeState::READY )
        {
            const auto authority = node_authorities_.at( node.get() );
            const auto base_path = std::filesystem::path( GetBaseWritePath( node ) );
            const auto network_config = base_path / "reviewed-trust-network.json";
            const auto database_path  = base_path / "reviewed-trust-globaldb";
            std::filesystem::create_directories( database_path );
            {
                std::ofstream config( network_config );
                ASSERT_TRUE( config.good() );
                config << R"({"pubsub_port":"0","pubsub_bind_address":"0.0.0.0","high_water":20,"low_water":1,"bootstrap_addresses":[")"
                       << node->GetPubSub()->GetInterfaceAddress() << R"("]})";
            }

            const std::string topic( TransactionManager::GNUS_FULL_NODES_TOPIC );
            sgns::crdt::GlobalDbNetworkComposition::Config composition_config;
            composition_config.network_config_path = network_config.string();
            composition_config.database_path       = database_path.string();
            composition_config.listen_topic        = topic;
            composition_config.broadcast_topic     = topic;
            auto composition_result =
                sgns::crdt::GlobalDbNetworkComposition::Create( std::move( composition_config ) );
            ASSERT_TRUE( composition_result.has_value() ) << composition_result.error().message();
            auto composition = composition_result.value();
            ASSERT_TRUE( composition->Start().has_value() );

            auto secure_crdt = std::make_shared<sgns::securecrdt::SecureCrdt>( composition->db(), topic );
            auto store = sgns::trustedpeer::TrustStateStore::Open(
                ( base_path / "reviewed-trust-state" ).string(), 144 );
            ASSERT_TRUE( store.has_value() ) << store.error().message();

            sgns::trustedpeer::GenesisManifest manifest;
            manifest.network_id              = 144;
            manifest.bootstrapper_public_key = authority->GetAddress();
            manifest.peers                   = { authority->GetAddress() };
            for ( const auto &peer : trust_peers_ )
            {
                manifest.peers.push_back( peer->GetAddress() );
            }
            manifest.membership_threshold = manifest.peers.size() / 2 + 1;
            manifest.burn_threshold       = manifest.peers.size() - manifest.peers.size() / 3;
            const auto canonical             = manifest.Canonicalized();
            ASSERT_TRUE( canonical.has_value() );
            const auto manifest_bytes = canonical->CanonicalBytes();
            ASSERT_TRUE( manifest_bytes.has_value() );

            auto controller = sgns::account::TrustStartupController::New(
                secure_crdt,
                store.value(),
                *canonical,
                authority->GetAddress(),
                [authority]( const std::vector<uint8_t> &bytes ) { return authority->Sign( bytes ); } );
            ASSERT_TRUE( controller.has_value() ) << controller.error().message();

            auto submission_crdt = std::make_shared<sgns::securecrdt::SecureCrdt>( composition->db(), topic );
            auto submission_store = sgns::trustedpeer::TrustStateStore::Open(
                ( base_path / "reviewed-trust-submitter-state" ).string(), 144 );
            ASSERT_TRUE( submission_store.has_value() ) << submission_store.error().message();
            auto registry = sgns::trustedpeer::TrustedPeerRegistry::NewProduction(
                submission_crdt,
                submission_store.value(),
                *canonical,
                authority->Sign( *manifest_bytes ),
                authority->GetAddress(),
                [authority]( const std::vector<uint8_t> &bytes ) { return authority->Sign( bytes ); } );
            ASSERT_TRUE( registry.has_value() ) << registry.error().message();
            auto submitted = registry.value()->SubmitReviewedGenesisApproval();
            ASSERT_TRUE( submitted.has_value() ) << submitted.error().message();
            // Poll-refresh rather than relying solely on candidate callbacks: on slow
            // (CI/Debug) runs the replicated approvals can arrive without the callback
            // firing in time, and the controller only re-evaluates on Refresh().
            sgns::test::assertWaitForCondition(
                [&]
                {
                    (void) controller.value()->Refresh();
                    return controller.value()->GetState() ==
                           sgns::account::TrustStartupController::State::ConfirmedReady;
                },
                std::chrono::milliseconds( 150000 ),
                "reviewed trust composition did not produce deterministic initial burn" );
            sgns::test::assertWaitForCondition(
                [&]() { return node->GetState() == GeniusNode::NodeState::READY; },
                std::chrono::milliseconds( 50000 ),
                "reviewed trust and deterministic initial burn did not unlock node: " + node->GetAddress() );
        }

        sgns::test::assertWaitForCondition( [&]() { return node->GetState() == GeniusNode::NodeState::READY; },
                                            std::chrono::milliseconds( 50000 ),
                                            "reviewed trust and deterministic initial burn did not unlock node: " +
                                                node->GetAddress() );
        ASSERT_EQ( node->GetState(), GeniusNode::NodeState::READY )
            << "final node state=" << static_cast<int>( node->GetState() );
    }

    void ConfigureConsensus( const std::shared_ptr<GeniusNode> &node,
                             size_t                             certificates_per_batch,
                             std::chrono::milliseconds          certificate_delay )
    {
        WaitForReady( node );
        sgns::test::assertWaitForCondition(
            [&]()
            {
                return node->GetState() == GeniusNode::NodeState::READY &&
                       sgns::MultiAccountTestAccess::GetValidatorRegistry( node );
            },
            std::chrono::milliseconds( 50000 ),
            "node blockchain not ready for consensus configuration: " + node->GetAddress() );
        ASSERT_EQ( node->GetState(), GeniusNode::NodeState::READY )
            << "consensus node final state=" << static_cast<int>( node->GetState() );

        ASSERT_TRUE(
            sgns::MultiAccountTestAccess::ConfigureConsensus( node, certificates_per_batch, certificate_delay ) );
    }

    void SetUp() override
    {
        GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                                                { return std::make_shared<MemorySecureStorage>( identifier ); } );

        auto binaryPath = boost::dll::program_location().parent_path();

        // Clean up any previous test runs
        for ( auto &entry : boost::filesystem::directory_iterator( binaryPath ) )
        {
            if ( entry.is_directory() && entry.path().filename().string().find( FILE_PREFIX ) != std::string::npos )
            {
                std::error_code ec;
                sgns::test::removeAllWithRetry( entry.path().string(), ec );
            }
        }
    }

    std::unordered_map<const GeniusNode *, std::string> node_base_paths_;
    std::unordered_map<const GeniusNode *, std::shared_ptr<GeniusAccount>> node_authorities_;
    std::shared_ptr<GeniusAccount> trust_authority_;
    std::vector<std::shared_ptr<GeniusAccount>> trust_peers_;
};

class ValidatorRegistryTest : public MultiAccountTest
{
};

TEST_F( ValidatorRegistryTest, MissingRegistryBlockIsFetchedFromPeerByCid )
{
    auto node_full   = CreateNode( "registry_cid_full", true, true, true );
    auto node_client = CreateNode( "registry_cid_client" );
    node_client->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );
    WaitForReady( node_full );
    WaitForReady( node_client );

    auto full_registry   = sgns::MultiAccountTestAccess::GetValidatorRegistry( node_full );
    auto client_registry = sgns::MultiAccountTestAccess::GetValidatorRegistry( node_client );
    ASSERT_TRUE( full_registry );
    ASSERT_TRUE( client_registry );

    ConfigureConsensus( node_full, 1, std::chrono::milliseconds( 100 ) );
    ConfigureConsensus( node_client, 1, std::chrono::milliseconds( 100 ) );

    auto registry_before = full_registry->LoadCurrentRegistry();
    ASSERT_TRUE( registry_before.has_value() );
    const auto initial_epoch = registry_before.value().epoch();

    auto mint_result = node_client->MintTokens( 100,
                                                sgns::test::NextMintSourceHash(),
                                                "test",
                                                TokenID::FromBytes( { 0x00 } ),
                                                "",
                                                std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint_result.has_value() );

    sgns::test::assertWaitForCondition(
        [&]()
        {
            auto full_state   = full_registry->LoadCurrentRegistry();
            auto client_state = client_registry->LoadCurrentRegistry();
            return full_state.has_value() && client_state.has_value() && full_state.value().epoch() > initial_epoch &&
                   client_state.value().epoch() == full_state.value().epoch() &&
                   client_registry->GetRegistryCid() == full_registry->GetRegistryCid();
        },
        std::chrono::milliseconds( 30000 ),
        "client did not receive the updated validator registry" );

    const auto registry_cid = full_registry->GetRegistryCid();
    const auto client_address = node_client->GetAddress();

    client_registry.reset();
    node_client.reset();

    node_client = CreateNode( "registry_cid_client" );
    ASSERT_TRUE( node_client );
    sgns::test::assertWaitForCondition(
        [&] { return static_cast<bool>( sgns::MultiAccountTestAccess::GetValidatorRegistry( node_client ) ); },
        std::chrono::milliseconds( 50000 ),
        "recovery client did not construct its empty validator registry" );
    client_registry = sgns::MultiAccountTestAccess::GetValidatorRegistry( node_client );
    ASSERT_TRUE( client_registry );
    EXPECT_EQ( node_client->GetAddress(), client_address );
    EXPECT_NE( client_registry->GetRegistryCid(), registry_cid );
    EXPECT_TRUE( client_registry->LoadRegistryByCid( registry_cid ).has_error() );

    node_client->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );
    WaitForReady( node_client );

    sgns::test::assertWaitForCondition(
        [&]()
        {
            auto loaded = client_registry->LoadRegistryByCid( registry_cid );
            return loaded.has_value();
        },
        std::chrono::milliseconds( 30000 ),
        "missing validator registry block was not fetched from the full node" );
}

TEST_F( MultiAccountTest, PersistedHistoricalTrustAndTransactionsRestartWithSingleManagerOwnership )
{
    auto historical_node = CreateNode( "persisted_historical_restart", true, true, true );
    ASSERT_TRUE( historical_node );
    WaitForReady( historical_node );
    ConfigureConsensus( historical_node, 1, std::chrono::milliseconds( 100 ) );

    const std::string historical_address   = historical_node->GetAddress();
    const std::string historical_base_path = GetBaseWritePath( historical_node );
    const auto        historical_mint      = historical_node->MintTokens(
        1000,
        sgns::test::NextMintSourceHash(),
        "test",
        TokenID::FromBytes( { 0x00 } ),
        "",
        std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( historical_mint.has_value() ) << historical_mint.error().message();
    const std::string historical_tx_hash = historical_mint.value().first;

    const auto historical_cid =
        sgns::MultiAccountTestAccess::ResolveAccountTransactionCid( historical_node, historical_tx_hash );
    ASSERT_TRUE( historical_cid.has_value() ) << historical_cid.error().message();
    ASSERT_FALSE( historical_cid.value().empty() );
    const auto historical_confirmed_count =
        historical_node->CountTransactions( TransactionManager::TransactionStatus::CONFIRMED );
    ASSERT_GE( historical_confirmed_count, 1U );

    historical_node.reset();

    // This is deliberately the fixture's existing-base-path branch. Do not replace
    // historical_base_path with a fresh client directory: trust and transaction
    // databases from the first lifetime are the subject of this counterexample.
    auto restarted_node = CreateNode( "persisted_historical_restart",
                                      true,
                                      true,
                                      true,
                                      historical_base_path );
    ASSERT_TRUE( restarted_node );
    ASSERT_EQ( GetBaseWritePath( restarted_node ), historical_base_path );
    ASSERT_EQ( restarted_node->GetAddress(), historical_address );
    WaitForReady( restarted_node );

    const auto restarted_manager = sgns::MultiAccountTestAccess::GetTransactionManager( restarted_node );
    ASSERT_TRUE( restarted_manager );
    EXPECT_EQ( sgns::MultiAccountTestAccess::GetTransactionManagerConstructionCount( restarted_node ), 1U );
    EXPECT_EQ( sgns::MultiAccountTestAccess::GetTransactionManagerStartCount( restarted_node ), 1U );

    const auto owner_generation =
        sgns::MultiAccountTestAccess::GetTransactionManagerOwnerGeneration( restarted_node );
    ASSERT_EQ( owner_generation, 1U );
    EXPECT_EQ( sgns::MultiAccountTestAccess::GetAccountTransactionCallbackOwnerGeneration( restarted_node ),
               owner_generation );
    EXPECT_EQ( sgns::MultiAccountTestAccess::GetBlockchainSlotHashOwnerGeneration( restarted_node ),
               owner_generation );
    EXPECT_EQ( sgns::MultiAccountTestAccess::GetManagerBurnProvider( restarted_node ).get(),
               sgns::MultiAccountTestAccess::GetNodeBurnProvider( restarted_node ).get() );

    sgns::test::assertWaitForCondition(
        [&]
        {
            return restarted_node->CountTransactions( TransactionManager::TransactionStatus::CONFIRMED ) >=
                   historical_confirmed_count;
        },
        std::chrono::milliseconds( 50000 ),
        "persisted transaction history did not reload from historical_base_path" );
    EXPECT_EQ( sgns::MultiAccountTestAccess::GetTransactionManager( restarted_node ).get(), restarted_manager.get() );
    const auto reopened_historical_cid =
        sgns::MultiAccountTestAccess::ResolveAccountTransactionCid( restarted_node, historical_tx_hash );
    ASSERT_TRUE( reopened_historical_cid.has_value() ) << reopened_historical_cid.error().message();
    EXPECT_EQ( reopened_historical_cid.value(), historical_cid.value() );

    const auto new_mint = restarted_node->MintTokens( 2000,
                                                       sgns::test::NextMintSourceHash(),
                                                       "test",
                                                       TokenID::FromBytes( { 0x00 } ),
                                                       "",
                                                       std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( new_mint.has_value() ) << new_mint.error().message();
    const auto new_cid =
        sgns::MultiAccountTestAccess::ResolveAccountTransactionCid( restarted_node, new_mint.value().first );
    ASSERT_TRUE( new_cid.has_value() ) << new_cid.error().message();
    ASSERT_FALSE( new_cid.value().empty() );
    EXPECT_EQ( restarted_node->CountTransactions( TransactionManager::TransactionStatus::CONFIRMED ),
               historical_confirmed_count + 1U );
    EXPECT_EQ( sgns::MultiAccountTestAccess::GetTransactionManager( restarted_node ).get(), restarted_manager.get() );
    EXPECT_EQ( sgns::MultiAccountTestAccess::GetTransactionManagerConstructionCount( restarted_node ), 1U );
    EXPECT_EQ( sgns::MultiAccountTestAccess::GetTransactionManagerStartCount( restarted_node ), 1U );
    EXPECT_EQ( sgns::MultiAccountTestAccess::GetAccountTransactionCallbackOwnerGeneration( restarted_node ),
               owner_generation );
    EXPECT_EQ( sgns::MultiAccountTestAccess::GetBlockchainSlotHashOwnerGeneration( restarted_node ),
               owner_generation );
}

TEST_F( MultiAccountTest, ConcurrentSelectAccountSnapshotsAndCatchupCallbacksStayGenerationConsistent )
{
    auto node = CreateNode( "concurrent_account_generation", true, false, true, {}, true );
    ASSERT_TRUE( node );
    WaitForReady( node );

    const auto original_address = node->GetAddress();
    const auto replacement_key  = DeterministicKey( "concurrent_account_generation_replacement" );
    ASSERT_TRUE( node->AddAccountWithKey( replacement_key.c_str() ).has_value() );
    const auto accounts = node->GetAvailableAccounts();
    const auto replacement = std::find_if( accounts.begin(), accounts.end(), [&]( const std::string &address )
                                           { return address != original_address; } );
    ASSERT_NE( replacement, accounts.end() );

    const auto stale_snapshot = sgns::MultiAccountTestAccess::SnapshotAccountGeneration( node );
    ASSERT_TRUE( stale_snapshot.account );
    ASSERT_TRUE( stale_snapshot.manager );

    std::mutex              selection_barrier_mutex;
    std::condition_variable selection_barrier_condition;
    size_t                  selection_barrier = 0;
    bool                    selection_barrier_open = false;
    auto arrive_at_selection_barrier = [&]
    {
        std::unique_lock<std::mutex> lock( selection_barrier_mutex );
        if ( ++selection_barrier == 3 )
        {
            selection_barrier_open = true;
            selection_barrier_condition.notify_all();
        }
        else
        {
            selection_barrier_condition.wait( lock, [&] { return selection_barrier_open; } );
        }
    };

    std::mutex observed_mutex;
    std::vector<sgns::MultiAccountTestAccess::AccountGenerationSnapshot> observed_generations;
    std::atomic_bool selector_done{ false };
    std::atomic_uint64_t stale_callback_side_effects{ 0 };

    std::thread selector(
        [&]
        {
            arrive_at_selection_barrier();
            for ( size_t iteration = 0; iteration < 3; ++iteration )
            {
                const auto &target = iteration % 2 == 0 ? *replacement : original_address;
                ASSERT_TRUE( node->SelectAccount( target ).has_value() );
                sgns::test::assertWaitForCondition(
                    [&] { return node->GetState() == GeniusNode::NodeState::READY; },
                    std::chrono::milliseconds( 50000 ),
                    "replacement account did not return to READY" );
            }
            selector_done.store( true );
        } );

    std::thread reader(
        [&]
        {
            arrive_at_selection_barrier();
            do
            {
                auto observed = sgns::MultiAccountTestAccess::SnapshotAccountGeneration( node );
                (void) node->GetAddress();
                (void) node->GetTransactionManager();
                (void) node->GetBalance();
                std::lock_guard<std::mutex> lock( observed_mutex );
                observed_generations.push_back( std::move( observed ) );
                std::this_thread::yield();
            } while ( !selector_done.load() );
        } );

    std::thread callback(
        [&]
        {
            arrive_at_selection_barrier();
            sgns::test::assertWaitForCondition(
                [&] { return node->GetAddress() != original_address; },
                std::chrono::milliseconds( 50000 ),
                "account selection did not publish a replacement" );
            const auto callback_generation = sgns::MultiAccountTestAccess::InjectCatchupCallback(
                node, stale_snapshot, stale_callback_side_effects );
            auto observed = sgns::MultiAccountTestAccess::SnapshotAccountGeneration( node );
            observed.catchup_generation = callback_generation;
            std::lock_guard<std::mutex> lock( observed_mutex );
            observed_generations.push_back( std::move( observed ) );
        } );

    selector.join();
    reader.join();
    callback.join();

    ASSERT_FALSE( observed_generations.empty() );
    for ( const auto &observed : observed_generations )
    {
        if ( !observed.account || !observed.manager ) continue;
        ASSERT_FALSE( observed.account_address.empty() );
        ASSERT_EQ( observed.account_address, observed.manager_address ) << "CR-11 generation mismatch";
        ASSERT_EQ( observed.generation, observed.catchup_generation ) << "CR-11 generation mismatch";
    }
    ASSERT_EQ( stale_callback_side_effects.load(), 0U ) << "CR-11 generation mismatch";
}

TEST_F( MultiAccountTest, SyncThroughEachOther )
{
    AddCanonicalTrustPeer( "node_multi_1" );
    // Create nodes dynamically
    auto node_full     = CreateNode( "node_multi_full", true, true, true );
    auto node_original = CreateNode( "node_multi_1" );
    node_original->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );
    WaitForReady( node_full );
    WaitForReady( node_original );

    auto balance_original_start = node_original->GetBalance();
    // Mint some tokens
    auto mint_result = node_original->MintTokens( 100,
                                                  sgns::test::NextMintSourceHash(),
                                                  "test",
                                                  sgns::TokenID::FromBytes( { 0x00 } ),
                                                  "",
                                                  std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out on node_original";

    mint_result = node_original->MintTokens( 2000,
                                             sgns::test::NextMintSourceHash(),
                                             "test",
                                             sgns::TokenID::FromBytes( { 0x00 } ),
                                             "",
                                             std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out on node_original";
    mint_result = node_original->MintTokens( 30,
                                             sgns::test::NextMintSourceHash(),
                                             "test",
                                             sgns::TokenID::FromBytes( { 0x00 } ),
                                             "",
                                             std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );

    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out on node_original";

    std::cout << " 3 mint transactions on original node completed, Creating duplicated node..." << std::endl;

    auto node_duplicated = CreateNode( "node_multi_1", false, true );
    node_duplicated->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );
    WaitForReady( node_duplicated );

    mint_result = node_duplicated->MintTokens( 60000,
                                               sgns::test::NextMintSourceHash(),
                                               "test",
                                               sgns::TokenID::FromBytes( { 0x00 } ),
                                               "",
                                               std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out on node_duplicated";

    sgns::test::assertWaitForCondition(
        [&] { return ( balance_original_start + 60000 + 2000 + 100 + 30 ) == node_duplicated->GetBalance(); },
        std::chrono::milliseconds( 30000 ),
        "node_duplicated balance not synced" );
    sgns::test::assertWaitForCondition(
        [&] { return ( balance_original_start + 60000 + 2000 + 100 + 30 ) == node_original->GetBalance(); },
        std::chrono::milliseconds( 30000 ),
        "node_duplicated balance not synced" );

    ASSERT_EQ( node_duplicated->GetBalance(), node_original->GetBalance() );
}

TEST_F( MultiAccountTest, CRDTFilterDuplicateTx )
{
    // Create 3 nodes - 2 with the same address, 1 different (full node for network)
    auto node_full        = CreateNode( "full_node_address_unique", true, true, true );
    auto node_same_addr_1 = CreateNode( "duplicate_address_12345" );
    node_same_addr_1->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );

    auto node_same_addr_2 = CreateNode( "duplicate_address_12345", false, true );
    node_same_addr_2->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );

    WaitForReady( node_full );
    WaitForReady( node_same_addr_1 );
    WaitForReady( node_same_addr_2 );

    // Verify nodes have the same address (they should since they use same self_address)
    ASSERT_EQ( node_same_addr_1->GetAddress(), node_same_addr_2->GetAddress() )
        << "Nodes with same self_address should have same address";

    std::cout << "Node 1 address: " << node_same_addr_1->GetAddress() << '\n';
    std::cout << "Node 2 address: " << node_same_addr_2->GetAddress() << '\n';
    std::cout << "Full node address: " << node_full->GetAddress() << '\n';

    // Get initial balances (should be 0)
    auto balance_node1_start = node_same_addr_1->GetBalance();
    auto balance_node2_start = node_same_addr_2->GetBalance();
    auto balance_full_start  = node_full->GetBalance();

    fmt::println( "Initial balances - Node1: {}, Node2: {}, Full: {}",
                  balance_node1_start,
                  balance_node2_start,
                  balance_full_start );

    // Get initial transaction counts
    auto tx_count_node1_start = node_same_addr_1->CountTransactions( TransactionManager::TransactionStatus::CONFIRMED );
    auto tx_count_node2_start = node_same_addr_2->CountTransactions( TransactionManager::TransactionStatus::CONFIRMED );
    auto tx_count_full_start  = node_full->CountTransactions( TransactionManager::TransactionStatus::CONFIRMED );

    fmt::println( "Initial tx counts - Node1: {}, Node2: {}, Full: {}",
                  tx_count_node1_start,
                  tx_count_node2_start,
                  tx_count_full_start );

    // Mint tokens on both nodes with same address BEFORE connecting them
    std::cout << "Minting tokens on isolated nodes..." << std::endl;

    auto mint_result_1 = node_same_addr_1->MintTokens( 50000000000, // 50 GNUS
                                                       sgns::test::NextMintSourceHash(),
                                                       "test",
                                                       sgns::TokenID::FromBytes( { 0x00 } ),
                                                       "",
                                                       std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint_result_1.has_value() ) << "Mint transaction failed on node_same_addr_1";

    std::cout << "Mint transaction 1 ID: " << mint_result_1.value().first << std::endl;

    sgns::test::assertWaitForCondition( [&]()
                                        { return node_same_addr_2->GetBalance() == balance_node2_start + 50000000000; },
                                        std::chrono::milliseconds( 30000 ),
                                        "node_same_addr_2 balance not synced" );
    //TODO - this is not working at the moment
    //auto mint_received = node_same_addr_2->WaitForTransactionOutgoing(
    //    mint_result_1.value().first,
    //    std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) );
    //EXPECT_EQ( mint_received, TransactionManager::TransactionStatus::CONFIRMED );

    // Check balances after minting but before connecting
    auto balance_node1_after_mint = node_same_addr_1->GetBalance();
    auto balance_node2_after_mint = node_same_addr_2->GetBalance();

    fmt::println( "Balances after minting (isolated) - Node1: {}, Node2: {}",
                  balance_node1_after_mint,
                  balance_node2_after_mint );

    // Both nodes should have their respective minted amounts since they're isolated
    ASSERT_EQ( balance_node1_after_mint, balance_node1_start + 50000000000 );
    ASSERT_EQ( balance_node2_after_mint, balance_node2_start + 50000000000 );

    // Now connect the nodes - this should trigger CRDT filter to resolve conflicts
    std::cout << "Creating conflicting transfers..." << std::endl;

    auto transfer1_res = node_same_addr_1->TransferFunds( 10000000000, // 10 GNUS
                                                          "0x00",
                                                          sgns::TokenID::FromBytes( { 0x00 } ) );

    ASSERT_TRUE( transfer1_res.has_value() ) << "Transfer 1 failed on node_same_addr_1";
    auto transfer2_res = node_same_addr_2->TransferFunds( 13000000000, // 13 GNUS
                                                          "0x00",
                                                          sgns::TokenID::FromBytes( { 0x00 } ) );

    ASSERT_TRUE( transfer2_res.has_value() ) << "Transfer 2 failed on node_same_addr_2";

    // Add peers to each node
    node_same_addr_2->AddPeers( { node_same_addr_1->GetPubSub()->GetInterfaceAddress() } );

    auto tx1_received = node_full->WaitForTransactionIncoming(
        transfer1_res.value(),
        std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) );

    fmt::println( "Waiting for the conflict resolution" );

    // Both transfers claim the same address+nonce, so exactly one can be certified. The
    // losing peer must learn that from the winner's certificate and fail its own
    // transaction, rather than sitting in VERIFYING until the proposal TTL expires.
    const auto conflict_start = std::chrono::steady_clock::now();

    uint64_t          correct_tokens_transferred = 0;
    sgns::GeniusNode *losing_node                = nullptr;
    std::string       losing_tx;
    sgns::test::assertWaitForCondition(
        [&]()
        {
            auto status1 = node_same_addr_1->GetTransactionStatus( transfer1_res.value() );
            if ( status1 == TransactionManager::TransactionStatus::CONFIRMED )
            {
                correct_tokens_transferred = 10000000000;
                losing_node                = node_same_addr_2.get();
                losing_tx                  = transfer2_res.value();
                return true;
            }

            auto status2 = node_same_addr_2->GetTransactionStatus( transfer2_res.value() );
            if ( status2 == TransactionManager::TransactionStatus::CONFIRMED )
            {
                correct_tokens_transferred = 13000000000;
                losing_node                = node_same_addr_1.get();
                losing_tx                  = transfer1_res.value();
                return true;
            }

            return false;
        },
        std::chrono::milliseconds( 50000 ),
        "Neither transfer was confirmed" );

    ASSERT_NE( losing_node, nullptr );

    // The whole point of the fix: the loser fails off the winner's certificate. The bound
    // is deliberately far below ConsensusManager::PendingLifecycleConfig::pending_ttl
    // (3 minutes), so a regression to "wait for the TTL" fails this test instead of
    // merely slowing it down.
    static constexpr auto kFailFastBudget = std::chrono::seconds( 60 );
    static_assert( kFailFastBudget < std::chrono::minutes( 3 ), "budget must beat the proposal TTL" );

    sgns::test::assertWaitForCondition(
        [&]()
        { return losing_node->GetTransactionStatus( losing_tx ) == TransactionManager::TransactionStatus::FAILED; },
        kFailFastBudget,
        "Losing transaction did not fail after the winner's certificate arrived" );

    const auto conflict_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - conflict_start );
    fmt::println( "Losing transaction {} failed {} ms after the conflict started",
                  losing_tx,
                  conflict_elapsed.count() );
    EXPECT_LT( conflict_elapsed, std::chrono::minutes( 3 ) )
        << "loser only resolved around the proposal TTL -- the certificate shortcut did not work";

    sgns::test::assertWaitForCondition(
        [&]() { return node_same_addr_1->GetBalance() == ( balance_node1_after_mint - correct_tokens_transferred ); },
        std::chrono::milliseconds( 50000 ),
        "node_same_addr_1 balance not synced" );
    sgns::test::assertWaitForCondition( [&]()
                                        { return node_same_addr_2->GetBalance() == node_same_addr_1->GetBalance(); },
                                        std::chrono::milliseconds( 50000 ),
                                        "node_same_addr_2 balance not synced" );

    fmt::println( "Balances after bootstrap - Node1: {}, Node2: {}",
                  node_same_addr_2->GetBalance(),
                  node_same_addr_1->GetBalance() );

    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );

    // Get final balances after CRDT resolution
    auto balance_node1_final = node_same_addr_1->GetBalance();
    auto balance_node2_final = node_same_addr_2->GetBalance();
    auto balance_full_final  = node_full->GetBalance( node_same_addr_1->GetAddress() );

    fmt::println( "Final balances after CRDT resolution - Node1: {}, Node2: {}, Full: {}",
                  balance_node1_final,
                  balance_node2_final,
                  balance_full_final );

    // Get final transaction counts
    auto tx_count_node1_final = node_same_addr_1->CountTransactions( TransactionManager::TransactionStatus::CONFIRMED );
    auto tx_count_node2_final = node_same_addr_2->CountTransactions( TransactionManager::TransactionStatus::CONFIRMED );

    fmt::println( "Final tx counts - Node1: {}, Node2: {}", tx_count_node1_final, tx_count_node2_final );

    // Since both nodes have the same address, they should have the same final balance
    ASSERT_EQ( balance_node1_final, balance_node2_final )
        << "Nodes with same address should have same balance after CRDT resolution";
    ASSERT_EQ( balance_full_final, balance_node2_final )
        << "Nodes with same address should have same balance after CRDT resolution";

    std::cout << "CRDT Filter test completed successfully!" << std::endl;
}

TEST_F( MultiAccountTest, NodeConsensusTest )
{
    constexpr size_t kCertificatesPerBatch = 1;
    const auto       kCertificateDelay     = std::chrono::seconds( 1 );

    auto node_full   = CreateNode( "node_consensus_full", true, true, true );
    auto node_client = CreateNode( "node_consensus_client" );
    auto node_peer1  = CreateNode( "node_consensus_peer1" );
    auto node_peer2  = CreateNode( "node_consensus_peer2" );
    auto node_peer3  = CreateNode( "node_consensus_peer3" );

    const std::array nodes = { node_full, node_client, node_peer1, node_peer2, node_peer3 };
    for ( size_t i = 1; i < nodes.size(); ++i )
    {
        nodes[i]->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );
    }
    for ( const auto &node : nodes )
    {
        ConfigureConsensus( node, kCertificatesPerBatch, kCertificateDelay );
    }

    auto registry = sgns::MultiAccountTestAccess::GetValidatorRegistry( node_full );
    ASSERT_TRUE( registry );

    fmt::println( "Nodes created. Registry loaded" );
    sgns::test::assertWaitForCondition(
        [&]()
        {
            auto load = registry->LoadCurrentRegistry();
            return load.has_value() && !registry->GetRegistryCid().empty();
        },
        std::chrono::milliseconds( 30000 ),
        "validator registry not initialized" );

    fmt::println( "Registry CID: {}", registry->GetRegistryCid() );
    auto assert_registry_updated = [&]( uint64_t epoch_before, const std::string &cid_before )
    {
        sgns::test::assertWaitForCondition(
            [&]()
            {
                auto load = registry->LoadCurrentRegistry();
                return load.has_value() && load.value().epoch() > epoch_before &&
                       registry->GetRegistryCid() != cid_before;
            },
            std::chrono::milliseconds( 30000 ),
            "validator registry did not update" );

        auto registry_after = registry->LoadCurrentRegistry();
        ASSERT_TRUE( registry_after.has_value() );
        EXPECT_GT( registry_after.value().epoch(), epoch_before );
        EXPECT_NE( registry->GetRegistryCid(), cid_before );

        if ( registry_after.value().validators().size() > 0 )
        {
            auto *full_validator = sgns::ValidatorRegistry::FindValidator( registry_after.value(),
                                                                           node_full->GetAddress() );
            ASSERT_TRUE( full_validator );
            EXPECT_GT( full_validator->weight(), 0 );
        }
        if ( registry_after.value().validators().size() > 1 )
        {
            auto *client_validator = sgns::ValidatorRegistry::FindValidator( registry_after.value(),
                                                                             node_client->GetAddress() );
            ASSERT_TRUE( client_validator );
            EXPECT_GT( client_validator->weight(), 0 );
        }
        if ( registry_after.value().validators().size() > 2 )
        {
            auto *peer1_validator = sgns::ValidatorRegistry::FindValidator( registry_after.value(),
                                                                            node_peer1->GetAddress() );
            ASSERT_TRUE( peer1_validator );
            EXPECT_GT( peer1_validator->weight(), 0 );
        }
        if ( registry_after.value().validators().size() > 3 )
        {
            auto *peer2_validator = sgns::ValidatorRegistry::FindValidator( registry_after.value(),
                                                                            node_peer2->GetAddress() );
            ASSERT_TRUE( peer2_validator );
            EXPECT_GT( peer2_validator->weight(), 0 );
        }
        if ( registry_after.value().validators().size() > 4 )
        {
            auto *peer3_validator = sgns::ValidatorRegistry::FindValidator( registry_after.value(),
                                                                            node_peer3->GetAddress() );
            ASSERT_TRUE( peer3_validator );

            EXPECT_GT( peer3_validator->weight(), 0 );
        }
    };

    auto wait_client_registry_caught_up = [&]()
    {
        auto client_registry = sgns::MultiAccountTestAccess::GetValidatorRegistry( node_client );
        ASSERT_TRUE( client_registry );

        sgns::test::assertWaitForCondition(
            [&]()
            {
                auto full_load   = registry->LoadCurrentRegistry();
                auto client_load = client_registry->LoadCurrentRegistry();
                return full_load.has_value() && client_load.has_value() &&
                       client_registry->GetRegistryCid() == registry->GetRegistryCid() &&
                       client_load.value().epoch() >= full_load.value().epoch();
            },
            std::chrono::milliseconds( 30000 ),
            "node_client validator registry not caught up" );
    };

    auto load_registry_state = [&]() -> std::pair<uint64_t, std::string>
    {
        auto state = registry->LoadCurrentRegistry();
        EXPECT_TRUE( state.has_value() );
        if ( !state.has_value() )
        {
            return { 0, "" };
        }
        return { state.value().epoch(), registry->GetRegistryCid() };
    };

    auto [epoch_before, cid_before] = load_registry_state();

    auto mint1 = node_client->MintTokens( 100,
                                          sgns::test::NextMintSourceHash(),
                                          "test",
                                          TokenID::FromBytes( { 0x00 } ) );
    ASSERT_TRUE( mint1.has_value() ) << "Mint 1 failed on node_client";
    fmt::println( "Mint 1 succeeded" );

    assert_registry_updated( epoch_before, cid_before );
    wait_client_registry_caught_up();

    std::tie( epoch_before, cid_before ) = load_registry_state();

    auto mint2 = node_client->MintTokens( 250,
                                          sgns::test::NextMintSourceHash(),
                                          "test",
                                          TokenID::FromBytes( { 0x00 } ) );
    ASSERT_TRUE( mint2.has_value() ) << "Mint 2 failed on node_client";
    fmt::println( "Mint 2 succeeded" );
    assert_registry_updated( epoch_before, cid_before );
    wait_client_registry_caught_up();
    std::tie( epoch_before, cid_before ) = load_registry_state();

    auto transfer1 = node_client->TransferFunds( 75,
                                                 node_peer1->GetAddress(),
                                                 sgns::TokenID::FromBytes( { 0x00 } ),
                                                 std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( transfer1.has_value() ) << "Transfer 1 failed on node_client";
    fmt::println( "Transfer 1 succeeded" );
    assert_registry_updated( epoch_before, cid_before );
    wait_client_registry_caught_up();
    std::tie( epoch_before, cid_before ) = load_registry_state();

    auto transfer2 = node_client->TransferFunds( 40,
                                                 node_peer2->GetAddress(),
                                                 sgns::TokenID::FromBytes( { 0x00 } ),
                                                 std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( transfer2.has_value() ) << "Transfer 2 failed on node_client";
    fmt::println( "Transfer 2 succeeded" );
    assert_registry_updated( epoch_before, cid_before );
    wait_client_registry_caught_up();
    std::tie( epoch_before, cid_before ) = load_registry_state();

    auto transfer3 = node_client->TransferFunds( 10,
                                                 node_peer3->GetAddress(),
                                                 sgns::TokenID::FromBytes( { 0x00 } ),
                                                 std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( transfer3.has_value() ) << "Transfer 3 failed on node_client";

    fmt::println( "Transfer 3 succeeded" );
    assert_registry_updated( epoch_before, cid_before );
}

TEST_F( MultiAccountTest, NodeConsensusBatch5Test )
{
    constexpr size_t kCertificatesPerBatch = 5;
    const auto       kCertificateDelay     = std::chrono::seconds( 1 );

    auto node_full   = CreateNode( "node_consensus_batch5_full", true, true, true );
    auto node_client = CreateNode( "node_consensus_batch5_client" );
    auto node_peer1  = CreateNode( "node_consensus_batch5_peer1" );
    auto node_peer2  = CreateNode( "node_consensus_batch5_peer2" );
    auto node_peer3  = CreateNode( "node_consensus_batch5_peer3" );

    const std::array nodes = { node_full, node_client, node_peer1, node_peer2, node_peer3 };
    for ( size_t i = 1; i < nodes.size(); ++i )
    {
        nodes[i]->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );
    }
    for ( const auto &node : nodes )
    {
        ConfigureConsensus( node, kCertificatesPerBatch, kCertificateDelay );
    }

    auto registry = sgns::MultiAccountTestAccess::GetValidatorRegistry( node_full );
    ASSERT_TRUE( registry );

    sgns::test::assertWaitForCondition(
        [&]()
        {
            auto load = registry->LoadCurrentRegistry();
            return load.has_value() && !registry->GetRegistryCid().empty();
        },
        std::chrono::milliseconds( 30000 ),
        "validator registry not initialized" );

    auto registry_state = registry->LoadCurrentRegistry();
    ASSERT_TRUE( registry_state.has_value() );
    const auto initial_epoch = registry_state.value().epoch();
    const auto initial_cid   = registry->GetRegistryCid();

    auto assert_registry_immutable = [&]( const char *step )
    {
        const auto deadline = std::chrono::steady_clock::now() + kCertificateDelay + std::chrono::seconds( 1 );
        while ( std::chrono::steady_clock::now() < deadline )
        {
            auto load = registry->LoadCurrentRegistry();
            ASSERT_TRUE( load.has_value() ) << "registry load failed during " << step;
            EXPECT_EQ( load.value().epoch(), initial_epoch ) << "registry epoch changed unexpectedly at " << step;
            EXPECT_EQ( registry->GetRegistryCid(), initial_cid ) << "registry CID changed unexpectedly at " << step;
            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        }
    };

    auto mint1 = node_client->MintTokens( 100,
                                          sgns::test::NextMintSourceHash(),
                                          "test",
                                          TokenID::FromBytes( { 0x00 } ),
                                          "",
                                          std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint1.has_value() ) << "Mint 1 failed on node_client";

    auto mint2 = node_client->MintTokens( 250,
                                          sgns::test::NextMintSourceHash(),
                                          "test",
                                          TokenID::FromBytes( { 0x00 } ),
                                          "",
                                          std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint2.has_value() ) << "Mint 2 failed on node_client";

    auto transfer1 = node_client->TransferFunds( 75,
                                                 node_peer1->GetAddress(),
                                                 TokenID::FromBytes( { 0x00 } ),
                                                 std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( transfer1.has_value() ) << "Transfer 1 failed on node_client";

    auto transfer2 = node_client->TransferFunds( 40,
                                                 node_peer2->GetAddress(),
                                                 TokenID::FromBytes( { 0x00 } ),
                                                 std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( transfer2.has_value() ) << "Transfer 2 failed on node_client";
    // Waiting once after the fourth certificate detects any premature update from the partial batch.
    assert_registry_immutable( "tx4" );

    auto transfer3 = node_client->TransferFunds( 10,
                                                 node_peer3->GetAddress(),
                                                 TokenID::FromBytes( { 0x00 } ),
                                                 std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( transfer3.has_value() ) << "Transfer 3 failed on node_client";

    sgns::test::assertWaitForCondition(
        [&]()
        {
            auto load = registry->LoadCurrentRegistry();
            return load.has_value() && load.value().epoch() > initial_epoch &&
                   registry->GetRegistryCid() != initial_cid;
        },
        std::chrono::milliseconds( 60000 ),
        "validator registry did not update after 5th certificate" );

    auto registry_after = registry->LoadCurrentRegistry();
    ASSERT_TRUE( registry_after.has_value() );
    EXPECT_GT( registry_after.value().epoch(), initial_epoch );
    EXPECT_NE( registry->GetRegistryCid(), initial_cid );

    const std::vector<std::string> expected_validators = { node_full->GetAddress(),
                                                           node_client->GetAddress(),
                                                           node_peer1->GetAddress(),
                                                           node_peer2->GetAddress(),
                                                           node_peer3->GetAddress() };
    for ( const auto &validator_id : expected_validators )
    {
        auto *validator = sgns::ValidatorRegistry::FindValidator( registry_after.value(), validator_id );
        ASSERT_TRUE( validator ) << "missing validator in registry: " << validator_id;
        EXPECT_GT( validator->weight(), 0 ) << "validator has non-positive weight: " << validator_id;
    }
}

// PROP-02: an Archive node is a passive replica — it tallies and stores everyone else's votes but
// never emits one of its own. Two independent assertions, because each has a different weakness:
//
//   1. Registry absence (primary). Voting is the ONLY path into the validator registry: votes from
//      addresses not yet in the registry are collected as "unregistered" and then inserted as
//      ACTIVE/REGULAR validators with nonzero weight. So an address that never appears in the
//      registry never voted, and unlike the certificate check this accumulates over every proposal
//      in the test and is permanent once it happens — it cannot be missed by timing.
//   2. Certificate voter set (corroboration). Weaker on its own, because the aggregator cuts a
//      certificate from whatever votes it holds at that instant, so a late vote can be absent from
//      a certificate without the node having abstained.
//
// Three positive controls guard against the failure mode that matters most here: if consensus never
// ran at all, "the archive did not vote" would be trivially true and the test would pass vacuously.
TEST_F( MultiAccountTest, ArchiveNodeAbstainsFromVoting )
{
    constexpr size_t kCertificatesPerBatch = 1;
    const auto       kCertificateDelay     = std::chrono::seconds( 1 );

    auto node_full    = CreateNode( "node_abstain_full", true, true, true );
    auto node_client  = CreateNode( "node_abstain_client" );
    auto node_peer1   = CreateNode( "node_abstain_peer1" );
    auto node_peer2   = CreateNode( "node_abstain_peer2" );
    auto node_archive = CreateNode( "node_abstain_archive",
                                    /*isFullNode=*/false,
                                    /*isProcessor=*/false,
                                    /*isGenesisAuthorized=*/false,
                                    /*existingBasePath=*/{},
                                    /*rpcCatchup=*/false,
                                    /*nodeTypeOverride=*/std::string( "Archive" ) );

    const std::array nodes = { node_full, node_client, node_peer1, node_peer2, node_archive };
    for ( size_t i = 1; i < nodes.size(); ++i )
    {
        nodes[i]->AddPeers( { node_full->GetPubSub()->GetInterfaceAddress() } );
    }
    for ( const auto &node : nodes )
    {
        ConfigureConsensus( node, kCertificatesPerBatch, kCertificateDelay );
    }

    // Fail loudly here rather than downstream if the role did not resolve.
    ASSERT_EQ( node_archive->GetNodeType(), sgns::GeniusNode::NodeType::Archive );
    const std::string archive_address = node_archive->GetAddress();

    auto registry = sgns::MultiAccountTestAccess::GetValidatorRegistry( node_full );
    ASSERT_TRUE( registry );
    sgns::test::assertWaitForCondition(
        [&]()
        {
            auto load = registry->LoadCurrentRegistry();
            return load.has_value() && !registry->GetRegistryCid().empty();
        },
        std::chrono::milliseconds( 30000 ),
        "validator registry not initialized" );

    auto registry_before = registry->LoadCurrentRegistry();
    ASSERT_TRUE( registry_before.has_value() );
    const auto epoch_before = registry_before.value().epoch();

    // Drive consensus with a transaction the archive neither authored nor received.
    auto mint = node_client->MintTokens( 100,
                                         sgns::test::NextMintSourceHash(),
                                         "test",
                                         TokenID::FromBytes( { 0x00 } ) );
    ASSERT_TRUE( mint.has_value() ) << "mint failed on node_client";

    // MintTokens returns once submitted; the UTXO has to settle before it is spendable. Wait on the
    // balance explicitly rather than relying on incidental delay from other assertions.
    sgns::test::assertWaitForCondition( [&]() { return node_client->GetBalance() >= 100; },
                                        std::chrono::milliseconds( 30000 ),
                                        "mint did not settle into node_client's balance" );

    auto transfer = node_client->TransferFunds( 75,
                                                node_peer1->GetAddress(),
                                                sgns::TokenID::FromBytes( { 0x00 } ),
                                                std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( transfer.has_value() ) << "transfer failed on node_client";

    // The nonce subject's hash IS the transaction hash (GetSubjectHash returns payload.tx_hash()
    // for BuiltinSubjectKind::Nonce), so the tx id looks the certificate up directly.
    const std::string subject_hash = transfer.value().first;

    // POSITIVE CONTROL 1: a certificate with at least one vote exists. assertWaitForCondition
    // FAILS the test on timeout, so "consensus never ran" can never masquerade as success.
    std::optional<sgns::ConsensusCertificate> certificate;
    sgns::test::assertWaitForCondition(
        [&]()
        {
            certificate = sgns::MultiAccountTestAccess::GetCertificate( node_full, subject_hash );
            return certificate.has_value() && certificate->votes_size() > 0;
        },
        std::chrono::milliseconds( 30000 ),
        "no certificate with votes formed for the transfer; the abstention assertion would be vacuous" );

    std::unordered_set<std::string> voters;
    for ( const auto &vote : certificate->votes() )
    {
        voters.insert( vote.voter_id() );
    }

    // POSITIVE CONTROL 2: a non-archive node voted in THIS certificate, so a missing archive
    // voter_id means abstention rather than an empty vote set.
    EXPECT_TRUE( voters.count( node_client->GetAddress() ) > 0 || voters.count( node_full->GetAddress() ) > 0 )
        << "no non-archive voter present in the certificate; the assertion below would be trivially true";

    EXPECT_EQ( voters.count( archive_address ), 0u ) << "archive node voted on proposal " << certificate->proposal_id();

    // PRIMARY ASSERTION: registry membership. Wait for the registry to advance first, so we are
    // inspecting a snapshot that consensus actually produced.
    sgns::test::assertWaitForCondition(
        [&]()
        {
            auto load = registry->LoadCurrentRegistry();
            return load.has_value() && load.value().epoch() > epoch_before;
        },
        std::chrono::milliseconds( 30000 ),
        "validator registry did not update" );

    auto registry_after = registry->LoadCurrentRegistry();
    ASSERT_TRUE( registry_after.has_value() );

    EXPECT_FALSE( sgns::ValidatorRegistry::FindValidator( registry_after.value(), archive_address ) )
        << "archive node was enrolled as a validator, which only casting a vote can cause";

    // POSITIVE CONTROL 3: a voting node IS enrolled in that same snapshot, so registry-absence is
    // evidence of abstention and not of an empty registry.
    EXPECT_TRUE( sgns::ValidatorRegistry::FindValidator( registry_after.value(), node_client->GetAddress() ) ||
                 sgns::ValidatorRegistry::FindValidator( registry_after.value(), node_full->GetAddress() ) )
        << "no non-archive validator enrolled either; the assertion above would be trivially true";
}
