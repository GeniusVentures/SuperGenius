#include "account/GeniusAccount.hpp"

#include <gtest/gtest.h>

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem/operations.hpp>

#include <fstream>

#include "account/BurnConfig.hpp"
#include "account/GeniusNode.hpp"
#include "account/TransactionManager.hpp"
#include "crdt/globaldb/GlobalDbNetworkComposition.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/wait_condition.hpp"
#include "trustedpeer/TrustStateStore.hpp"
#include "trustedpeer/TrustedPeerRegistry.hpp"
#include "trustedpeer/genesis_tool/LocalTrustAdmin.hpp"

namespace sgns
{
    class MultiAccountTestAccess
    {
    public:
        struct PolicySnapshot
        {
            const void *registry;
            const void *secure_crdt;
            const void *trust_store;
            const void *trusted_peer_registry;
            const void *burn_config;
            const void *confirmed_provider;
            size_t      policy_registrations;
            size_t      candidate_registrations;
        };

        static PolicySnapshot Snapshot( const std::shared_ptr<GeniusNode> &node )
        {
            auto provider = node->burn_config_->GetConfirmedValueProvider();
            return { &node->secure_crdt_->Registry(),
                     node->secure_crdt_.get(),
                     node->trust_state_store_.get(),
                     node->trusted_peer_registry_.get(),
                     node->burn_config_.get(),
                     provider.get(),
                     node->secure_crdt_->Registry().AllEntries().size(),
                     node->secure_crdt_->Registry().AllCandidateDomains().size() };
        }

        static std::shared_ptr<securecrdt::SecureCrdt> SecureCrdt( const std::shared_ptr<GeniusNode> &node )
        {
            return node->secure_crdt_;
        }

        static std::shared_ptr<trustedpeer::TrustStateStore> Store( const std::shared_ptr<GeniusNode> &node )
        {
            return node->trust_state_store_;
        }

        static std::shared_ptr<trustedpeer::TrustedPeerRegistry> Registry( const std::shared_ptr<GeniusNode> &node )
        {
            return node->trusted_peer_registry_;
        }

        static std::shared_ptr<account::BurnConfig> Burn( const std::shared_ptr<GeniusNode> &node )
        {
            return node->burn_config_;
        }

        static std::shared_ptr<TransactionManager> Manager( const std::shared_ptr<GeniusNode> &node )
        {
            return node->transaction_manager_;
        }

        static const void *Account( const std::shared_ptr<GeniusNode> &node )
        {
            return node->account_.get();
        }

        static std::shared_ptr<const account::ConfirmedBurnValueProvider> ManagerProvider(
            const std::shared_ptr<TransactionManager> &manager )
        {
            return manager->confirmed_burn_provider_;
        }

        static std::shared_ptr<TransactionManager> NewPreReadyManager( const std::shared_ptr<GeniusNode> &node )
        {
            return TransactionManager::New( node->tx_globaldb_,
                                            node->io_,
                                            node->account_,
                                            node->blockchain_,
                                            node->node_type_,
                                            node->subnet_id_,
                                            std::chrono::milliseconds( 300000 ),
                                            std::chrono::milliseconds( 0 ),
                                            account::BurnConfig::GENESIS_DEFAULT_BASIS_POINTS,
                                            node->burn_config_->GetConfirmedValueProvider() );
        }
    };
} // namespace sgns

namespace
{
    using namespace sgns;
    using namespace sgns::account;
    using namespace sgns::trustedpeer;

    constexpr char PRIMARY_KEY[] = "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaa";
    constexpr char SECONDARY_KEY[] = "2071868aaf52ce5451a533dc5d9050c2024183e0dcb6bb55777c4ba617c6009f";
    const TokenID TOKEN_ID = TokenID::FromBytes( { 0x00 } );

    class PolicyLifetimeMultiAccountTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            path_ = boost::dll::program_location().parent_path() / "policy_lifetime_multi_account";
            test::removeAllWithRetry( path_.string() );
            boost::filesystem::create_directories( path_ );
            GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                { return std::make_shared<MemorySecureStorage>( identifier ); } );

            bootstrap_account_ = GeniusAccount::NewFromPrivateKey( TOKEN_ID, PRIMARY_KEY, path_ );
            ASSERT_TRUE( bootstrap_account_ );
            ASSERT_TRUE( GeniusNode::WriteNetworkConfig( path_.generic_string() + '/', 0, false ).has_value() );
            std::ofstream config( ( path_ / "sgns_config.json" ).string() );
            ASSERT_TRUE( config.good() );
            config << "{\"net_id\":144,\"subnet_id\":144,\"node_type\":\"Full\","
                      "\"is_processor\":false,\"rpc_catchup\":false,\"trusted_peers\":[\""
                   << bootstrap_account_->GetAddress() << "\"],\"bootstrapper_node\":\""
                   << bootstrap_account_->GetAddress()
                   << "\",\"trusted_peer_quorum_threshold\":1,\"burn_config_quorum_threshold\":1}";
            config.close();

            Blockchain::SetAuthorizedFullNodeAddress( bootstrap_account_->GetAddress() );
            node_ = GeniusNode::New( { "0xcafe", "0.65", "1.0", TOKEN_ID, path_.generic_string() + '/' },
                                     FromPrivateKey{ PRIMARY_KEY } );
            ASSERT_TRUE( node_ );
            Blockchain::SetAuthorizedFullNodeAddress( node_->GetAddress() );
            test::assertWaitForCondition(
                [&] { return node_->GetState() == GeniusNode::NodeState::WAITING_FOR_TRUST_GENESIS; },
                std::chrono::seconds( 50 ),
                "node did not enter restricted trust wait" );
        }

        void TearDown() override
        {
            node_.reset();
            bootstrap_account_.reset();
            GeniusAccount::SetSecureStorageFactory( nullptr );
            test::removeAllWithRetry( path_.string() );
        }

        GenesisManifest Manifest() const
        {
            GenesisManifest manifest;
            manifest.network_id              = 144;
            manifest.bootstrapper_public_key = bootstrap_account_->GetAddress();
            manifest.peers                   = { bootstrap_account_->GetAddress() };
            manifest.membership_threshold    = 1;
            manifest.burn_threshold          = 1;
            return manifest;
        }

        void ConfirmTrust()
        {
            const auto network_config = path_ / "reviewed-trust-network.json";
            const auto database_path  = path_ / "reviewed-trust-globaldb";
            boost::filesystem::create_directories( database_path );
            {
                std::ofstream config( network_config.string() );
                ASSERT_TRUE( config.good() );
                config << R"({"pubsub_port":"0","pubsub_bind_address":"0.0.0.0","high_water":20,"low_water":1,"bootstrap_addresses":[")"
                       << node_->GetPubSub()->GetInterfaceAddress() << R"("]})";
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

            auto secure_crdt = std::make_shared<securecrdt::SecureCrdt>( composition->db(), topic );
            auto store = TrustStateStore::Open( ( path_ / "reviewed-trust-state" ).string(), 144 );
            ASSERT_TRUE( store.has_value() ) << store.error().message();
            const auto manifest = Manifest().Canonicalized();
            ASSERT_TRUE( manifest.has_value() );
            const auto manifest_bytes = manifest->CanonicalBytes();
            ASSERT_TRUE( manifest_bytes.has_value() );

            auto registry = TrustedPeerRegistry::NewProduction(
                secure_crdt,
                store.value(),
                *manifest,
                bootstrap_account_->Sign( *manifest_bytes ),
                bootstrap_account_->GetAddress(),
                [account = bootstrap_account_]( const std::vector<uint8_t> &bytes ) { return account->Sign( bytes ); } );
            ASSERT_TRUE( registry.has_value() ) << registry.error().message();
            ASSERT_TRUE( secure_crdt->RegisterFilters() );
            auto submitted = registry.value()->SubmitReviewedGenesisApproval();
            ASSERT_TRUE( submitted.has_value() ) << submitted.error().message();

            test::assertWaitForCondition( [&] { return node_->GetState() == GeniusNode::NodeState::READY; },
                                          std::chrono::seconds( 50 ),
                                          "reviewed trust and deterministic initial burn did not unlock transaction services" );
        }

        boost::filesystem::path       path_;
        std::shared_ptr<GeniusAccount> bootstrap_account_;
        std::shared_ptr<GeniusNode>    node_;
    };
} // namespace

TEST_F( PolicyLifetimeMultiAccountTest, PolicyObjectsAndProviderSurviveRepeatedAccountSelection )
{
    auto pre_ready_manager = MultiAccountTestAccess::NewPreReadyManager( node_ );
    ASSERT_TRUE( pre_ready_manager );
    const auto before_rejected_count = pre_ready_manager->CountTransactions();
    const auto pre_ready = pre_ready_manager->PayEscrow( "", SGProcessing::TaskResult{}, nullptr );
    ASSERT_TRUE( pre_ready.has_error() );
    EXPECT_EQ( pre_ready.error(), make_error_code( TransactionManager::Error::TRUST_POLICY_NOT_READY ) );
    EXPECT_EQ( pre_ready_manager->CountTransactions(), before_rejected_count );
    pre_ready_manager.reset();

    ConfirmTrust();
    const auto initial_policy  = MultiAccountTestAccess::Snapshot( node_ );
    const auto initial_manager = MultiAccountTestAccess::Manager( node_ );
    const auto initial_account = MultiAccountTestAccess::Account( node_ );
    auto provider = MultiAccountTestAccess::Burn( node_ )->GetConfirmedValueProvider();
    ASSERT_TRUE( provider->IsReady() );
    ASSERT_EQ( provider->GetBasisPoints(), 100U );
    ASSERT_EQ( MultiAccountTestAccess::ManagerProvider( MultiAccountTestAccess::Manager( node_ ) ).get(),
               provider.get() );

    ASSERT_TRUE( node_->AddAccountWithKey( SECONDARY_KEY ).has_value() );
    const auto accounts = node_->GetAvailableAccounts();
    const auto secondary = std::find_if(
        accounts.begin(), accounts.end(), [&]( const std::string &address ) { return address != node_->GetAddress(); } );
    ASSERT_NE( secondary, accounts.end() );
    const auto primary = node_->GetAddress();

    ASSERT_TRUE( node_->SelectAccount( *secondary ).has_value() );
    test::assertWaitForCondition( [&] { return node_->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::seconds( 50 ),
                                  "secondary account transaction services did not become ready" );
    const auto secondary_policy = MultiAccountTestAccess::Snapshot( node_ );
    EXPECT_EQ( initial_policy.registry, secondary_policy.registry );
    EXPECT_EQ( initial_policy.secure_crdt, secondary_policy.secure_crdt );
    EXPECT_EQ( initial_policy.trust_store, secondary_policy.trust_store );
    EXPECT_EQ( initial_policy.trusted_peer_registry, secondary_policy.trusted_peer_registry );
    EXPECT_EQ( initial_policy.burn_config, secondary_policy.burn_config );
    EXPECT_EQ( initial_policy.confirmed_provider, secondary_policy.confirmed_provider );
    EXPECT_EQ( initial_policy.policy_registrations, secondary_policy.policy_registrations );
    EXPECT_EQ( initial_policy.candidate_registrations, secondary_policy.candidate_registrations );
    EXPECT_NE( initial_manager.get(), MultiAccountTestAccess::Manager( node_ ).get() );
    EXPECT_NE( initial_account, MultiAccountTestAccess::Account( node_ ) );
    EXPECT_EQ( MultiAccountTestAccess::ManagerProvider( MultiAccountTestAccess::Manager( node_ ) ).get(),
               provider.get() );

    const auto secondary_manager = MultiAccountTestAccess::Manager( node_ );
    ASSERT_TRUE( node_->SelectAccount( primary ).has_value() );
    test::assertWaitForCondition( [&] { return node_->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::seconds( 50 ),
                                  "primary account transaction services did not become ready again" );
    const auto restored_policy = MultiAccountTestAccess::Snapshot( node_ );
    EXPECT_EQ( initial_policy.registry, restored_policy.registry );
    EXPECT_EQ( initial_policy.secure_crdt, restored_policy.secure_crdt );
    EXPECT_EQ( initial_policy.trust_store, restored_policy.trust_store );
    EXPECT_EQ( initial_policy.trusted_peer_registry, restored_policy.trusted_peer_registry );
    EXPECT_EQ( initial_policy.burn_config, restored_policy.burn_config );
    EXPECT_EQ( initial_policy.confirmed_provider, restored_policy.confirmed_provider );
    EXPECT_EQ( initial_policy.policy_registrations, restored_policy.policy_registrations );
    EXPECT_EQ( initial_policy.candidate_registrations, restored_policy.candidate_registrations );
    EXPECT_NE( secondary_manager.get(), MultiAccountTestAccess::Manager( node_ ).get() );

    LocalTrustAdmin admin( MultiAccountTestAccess::Registry( node_ ), MultiAccountTestAccess::Burn( node_ ) );
    auto proposed = admin.ProposeBurn( 250 );
    ASSERT_TRUE( proposed.has_value() ) << proposed.error().message();
    test::assertWaitForCondition( [&] { return provider->GetBasisPoints() == 250U; },
                                  std::chrono::seconds( 50 ),
                                  "post-switch burn successor was not published" );
    EXPECT_EQ( provider->GetBasisPoints(), 250U );
    EXPECT_EQ( MultiAccountTestAccess::ManagerProvider( MultiAccountTestAccess::Manager( node_ ) ).get(),
               provider.get() );
}
