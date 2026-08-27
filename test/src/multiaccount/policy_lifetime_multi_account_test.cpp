#include "account/GeniusAccount.hpp"

#include <gtest/gtest.h>

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem/operations.hpp>

#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <optional>
#include <vector>

#include "account/BurnConfig.hpp"
#include "account/EscrowTransaction.hpp"
#include "account/GeniusNode.hpp"
#include "account/GeniusSigner.hpp"
#include "account/TransactionManager.hpp"
#include "account/TrustStartupController.hpp"
#include "account/TransferTransaction.hpp"
#include "crdt/globaldb/GlobalDbNetworkComposition.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "multisig/MultiSig.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "storage/rocksdb/rocksdb.hpp"
#include "storage/rocksdb/rocksdb_batch.hpp"
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
            return node ? node->SnapshotAccountServices().manager : nullptr;
        }

        static std::string ManagerAccountAddress( const std::shared_ptr<GeniusNode> &node )
        {
            const auto manager = Manager( node );
            return manager && manager->account_m ? manager->account_m->GetAddress()
                     : std::string{};
        }

        static securecrdt::CandidateApprovalRecord AttemptedTrustApproval(
            const std::shared_ptr<GeniusNode> &node,
            const securecrdt::CandidateCore   &canonical_candidate,
            const std::string                 &pinned_trust_address )
        {
            const auto bytes = canonical_candidate.CanonicalBytes().value();
            return { securecrdt::CandidateApprovalRecord::ENCODING_VERSION,
                     canonical_candidate,
                     pinned_trust_address,
                     node->trust_signer_->Sign( bytes ) };
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
            return NewManager( node, node->burn_config_->GetConfirmedValueProvider() );
        }

        static std::shared_ptr<TransactionManager> ReplaceManager(
            const std::shared_ptr<GeniusNode>                              &node,
            std::shared_ptr<const account::ConfirmedBurnValueProvider> provider )
        {
            if ( node->transaction_manager_ )
            {
                node->transaction_manager_->Stop();
                node->transaction_manager_.reset();
            }
            node->transaction_manager_ = NewManager( node, std::move( provider ) );
            return node->transaction_manager_;
        }

        static std::string StoreEscrow( const std::shared_ptr<GeniusNode> &node, uint64_t amount )
        {
            const TokenID token_id = TokenID::FromBytes( { 0x00 } );
            const std::string lock_id = "0x" + std::string( 64, '1' );
            SGTransaction::DAGStruct dag;
            dag.set_nonce( node->account_->ReserveNextNonce() );
            dag.set_source_addr( node->account_->GetAddress() );
            dag.set_uncle_hash( lock_id );
            dag.set_timestamp( std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::system_clock::now().time_since_epoch() )
                                   .count() );
            UTXOTxParameters params;
            params.second.push_back( { amount, lock_id, token_id } );
            auto escrow = std::make_shared<EscrowTransaction>(
                EscrowTransaction::New( std::move( params ), amount, node->account_->GetAddress(), 0, std::move( dag ) ) );
            escrow->MakeSignature( *node->account_ );
            crdt::GlobalDB::Buffer data;
            data.put( escrow->SerializeByteVector() );
            const auto path = TransactionManager::GetTransactionPath( *escrow );
            auto stored = node->tx_globaldb_->Put(
                crdt::HierarchicalKey( path ), data, { node->account_->GetAddress() } );
            return stored.has_value() ? path : std::string{};
        }

        static uint64_t PayEscrowAndReadBurn( const std::shared_ptr<GeniusNode> &node,
                                              const std::string                 &escrow_path )
        {
            SGProcessing::TaskResult result;
            auto *subtask = result.add_subtask_results();
            subtask->set_node_address( node->account_->GetAddress() );
            const auto token_id = TokenID::FromBytes( { 0x00 } );
            const auto &bytes = token_id.bytes();
            subtask->set_token_id( bytes.data(), bytes.size() );
            auto paid = node->transaction_manager_->PayEscrow( escrow_path, result, nullptr );
            if ( paid.has_error() ) return 0;
            constexpr std::string_view zero = "0x0000000000000000000000000000000000000000";
            for ( auto serialized : node->transaction_manager_->GetOutTransactions() )
            {
                auto transaction = TransactionManager::DeSerializeTransaction( base::Buffer( std::move( serialized ) ) );
                if ( transaction.has_error() || transaction.value()->GetHash() != paid.value() ) continue;
                auto transfer = std::dynamic_pointer_cast<TransferTransaction>( transaction.value() );
                if ( !transfer ) return 0;
                const auto outputs = transfer->GetDstInfos();
                const auto burn = std::find_if( outputs.begin(), outputs.end(), [zero]( const OutputDestInfo &output )
                                                { return output.dest_address == zero; } );
                return burn == outputs.end() ? 0 : burn->encrypted_amount;
            }
            return 0;
        }

    private:
        static std::shared_ptr<TransactionManager> NewManager(
            const std::shared_ptr<GeniusNode>                              &node,
            std::shared_ptr<const account::ConfirmedBurnValueProvider> provider )
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
                                            std::move( provider ) );
        }
    };
} // namespace sgns

namespace
{
    using namespace sgns;
    using namespace sgns::account;
    using namespace sgns::securecrdt;
    using namespace sgns::trustedpeer;

    constexpr char OPERATOR_A_KEY[] = "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaa";
    constexpr char OPERATOR_B_KEY[] = "2071868aaf52ce5451a533dc5d9050c2024183e0dcb6bb55777c4ba617c6009f";
    constexpr char PASSIVE_C_KEY[]  = "03071868aaf52ce5451a533dc5d9050c2024183e0dcb6bb55777c4ba617c6011";
    constexpr char PASSIVE_C_SECONDARY_KEY[] =
        "04071868aaf52ce5451a533dc5d9050c2024183e0dcb6bb55777c4ba617c6021";
    const TokenID TOKEN_ID = TokenID::FromBytes( { 0x00 } );

    outcome::result<void> CommitBatch( storage::rocksdb                         &database,
                                       const std::vector<TrustStateStore::Write> &writes )
    {
        auto batch = database.batch();
        if ( !batch ) return outcome::failure( std::errc::io_error );
        for ( const auto &[key, value] : writes )
        {
            auto put = batch->put( key, value );
            if ( put.has_error() ) return put.error();
        }
        return batch->commit();
    }

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
        }

        void TearDown() override
        {
            GeniusAccount::SetSecureStorageFactory( nullptr );
            test::removeAllWithRetry( path_.string() );
        }

        static void WriteNetworkConfig( const boost::filesystem::path &base_path,
                                        const std::optional<std::string> &bootstrap = std::nullopt )
        {
            boost::filesystem::create_directories( base_path );
            std::ofstream config( ( base_path / "network_config.json" ).string() );
            ASSERT_TRUE( config.good() );
            config << R"({"pubsub_port":"0","pubsub_bind_address":"0.0.0.0","upnp_enabled":false,"high_water":20,"low_water":1,"port_seed":0,"auto_dht":false,"bootstrap_addresses":[)";
            if ( bootstrap ) config << '"' << *bootstrap << '"';
            config << "]}";
        }

        static void WriteSgnsConfig( const boost::filesystem::path &base_path,
                                     const std::vector<std::string> &peers,
                                     const std::string &bootstrapper )
        {
            std::ofstream config( ( base_path / "sgns_config.json" ).string() );
            ASSERT_TRUE( config.good() );
            config << R"({"net_id":144,"subnet_id":144,"node_type":"Full","is_processor":false,"rpc_catchup":false,"trusted_peers":[)";
            for ( size_t i = 0; i < peers.size(); ++i )
            {
                if ( i != 0 ) config << ',';
                config << '"' << peers[i] << '"';
            }
            config << R"(],"bootstrapper_node":")" << bootstrapper
                   << R"(","trusted_peer_quorum_threshold":2,"burn_config_quorum_threshold":2})";
        }

        static std::shared_ptr<GeniusNode> NewNode( const boost::filesystem::path &base_path,
                                                    const char                     *private_key,
                                                    const std::vector<std::string> &peers,
                                                    const std::string              &bootstrapper,
                                                    const std::optional<std::string> &network_bootstrap = std::nullopt )
        {
            WriteNetworkConfig( base_path, network_bootstrap );
            WriteSgnsConfig( base_path, peers, bootstrapper );
            GeniusNodeConfig config{ "0xcafe", "0.65", "1.0", TOKEN_ID, base_path.generic_string() + '/' };
            return GeniusNode::New( config, FromPrivateKey{ private_key } );
        }

        static void WaitForTrustLifecycle( const std::shared_ptr<GeniusNode> &node )
        {
            ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition(
                [&]
                {
                    return node->GetState() == GeniusNode::NodeState::WAITING_FOR_TRUST_GENESIS ||
                           node->GetState() == GeniusNode::NodeState::WAITING_FOR_BURN_GENESIS ||
                           node->GetState() == GeniusNode::NodeState::READY;
                },
                std::chrono::seconds( 50 ),
                "node did not reach a trust lifecycle state" ) );
        }

        struct GenesisTool
        {
            std::shared_ptr<crdt::GlobalDbNetworkComposition> composition;
            std::shared_ptr<securecrdt::SecureCrdt>           secure;
            std::shared_ptr<TrustStateStore>                  store;
            std::shared_ptr<TrustedPeerRegistry>              registry;
            std::shared_ptr<BurnConfig>                       burn;

            ~GenesisTool()
            {
                burn.reset();
                registry.reset();
                secure.reset();
                store.reset();
                if ( composition ) composition->Stop();
            }
        };

        std::unique_ptr<GenesisTool> SubmitReviewedGenesis(
            const std::string                    &name,
            const std::string                    &bootstrap_address,
            const GenesisManifest                &manifest,
            const std::shared_ptr<GeniusAccount> &bootstrapper )
        {
            auto tool = std::make_unique<GenesisTool>();
            const auto config_path   = path_ / ( name + "-network.json" );
            const auto database_path = path_ / ( name + "-globaldb" );
            boost::filesystem::create_directories( database_path );
            {
                std::ofstream config( config_path.string() );
                EXPECT_TRUE( config.good() );
                config << R"({"pubsub_port":"0","pubsub_bind_address":"0.0.0.0","high_water":20,"low_water":1,"bootstrap_addresses":[")"
                       << bootstrap_address << R"("]})";
            }
            crdt::GlobalDbNetworkComposition::Config tool_config;
            tool_config.network_config_path = config_path.string();
            tool_config.database_path       = database_path.string();
            tool_config.listen_topic        = std::string( TransactionManager::GNUS_FULL_NODES_TOPIC );
            tool_config.broadcast_topic     = std::string( TransactionManager::GNUS_FULL_NODES_TOPIC );
            auto composition = crdt::GlobalDbNetworkComposition::Create( std::move( tool_config ) );
            EXPECT_TRUE( composition.has_value() );
            if ( !composition ) return {};
            tool->composition = composition.value();
            EXPECT_TRUE( tool->composition->Start().has_value() );
            tool->secure = std::make_shared<SecureCrdt>(
                tool->composition->db(), std::string( TransactionManager::GNUS_FULL_NODES_TOPIC ) );
            tool->store = TrustStateStore::Open( ( path_ / ( name + "-trust" ) ).string(), manifest.network_id ).value();
            const auto manifest_bytes = manifest.CanonicalBytes().value();
            tool->registry = TrustedPeerRegistry::NewProduction(
                tool->secure,
                tool->store,
                manifest,
                bootstrapper->Sign( manifest_bytes ),
                bootstrapper->GetAddress(),
                [bootstrapper]( const std::vector<uint8_t> &bytes ) { return bootstrapper->Sign( bytes ); } ).value();
            tool->burn = BurnConfig::NewProduction(
                tool->secure,
                tool->registry,
                tool->store,
                bootstrapper->GetAddress(),
                [bootstrapper]( const std::vector<uint8_t> &bytes ) { return bootstrapper->Sign( bytes ); } ).value();
            EXPECT_TRUE( tool->secure->RegisterFilters() );
            EXPECT_TRUE( tool->registry->SubmitReviewedGenesisApproval().has_value() );
            return tool;
        }

        boost::filesystem::path path_;
    };
} // namespace

TEST_F( PolicyLifetimeMultiAccountTest, ActiveTrustSignerSurvivesAccountSwitchBeforeInitialBurnReadiness )
{
    auto operator_a_signer = GeniusAccount::NewFromPrivateKey( TOKEN_ID, OPERATOR_A_KEY, path_ / "before-a" );
    auto operator_b_signer = GeniusAccount::NewFromPrivateKey( TOKEN_ID, OPERATOR_B_KEY, path_ / "before-b" );
    ASSERT_TRUE( operator_a_signer );
    ASSERT_TRUE( operator_b_signer );
    const std::vector<std::string> peers{ operator_a_signer->GetAddress(), operator_b_signer->GetAddress() };

    Blockchain::SetAuthorizedFullNodeAddress( operator_a_signer->GetAddress() );
    auto node_a = NewNode( path_ / "before-a", OPERATOR_A_KEY, peers, operator_a_signer->GetAddress() );
    ASSERT_TRUE( node_a );
    const auto bootstrap_address = node_a->GetPubSub()->GetInterfaceAddress();
    auto node_b = NewNode( path_ / "before-b", OPERATOR_B_KEY, peers, operator_a_signer->GetAddress(), bootstrap_address );
    ASSERT_TRUE( node_b );
    WaitForTrustLifecycle( node_a );
    WaitForTrustLifecycle( node_b );

    const std::string pinned_trust_address = operator_a_signer->GetAddress();
    ASSERT_TRUE( node_a->AddAccountWithKey( PASSIVE_C_SECONDARY_KEY ).has_value() );
    const auto accounts = node_a->GetAvailableAccounts();
    const auto replacement = std::find_if( accounts.begin(), accounts.end(), [&]( const std::string &address )
                                           { return address != pinned_trust_address; } );
    ASSERT_NE( replacement, accounts.end() );
    const std::string replacement_address = *replacement;
    ASSERT_TRUE( node_a->SelectAccount( replacement_address ).has_value() );

    GenesisManifest manifest;
    manifest.network_id              = 144;
    manifest.bootstrapper_public_key = pinned_trust_address;
    manifest.peers                   = peers;
    manifest.membership_threshold    = 2;
    manifest.burn_threshold          = 2;
    manifest                         = manifest.Canonicalized().value();
    auto tool = SubmitReviewedGenesis( "before-tool", bootstrap_address, manifest, operator_a_signer );
    ASSERT_TRUE( tool );

    ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition(
        [&] { return MultiAccountTestAccess::Store( node_a )->LoadAndVerify().has_value(); },
        std::chrono::seconds( 30 ),
        "switched active member did not persist reviewed genesis" ) );
    const auto snapshot = MultiAccountTestAccess::Store( node_a )->LoadAndVerify().value();
    const auto canonical_candidate = BurnConfig::BurnCandidateCore( snapshot.burn ).value();
    const auto candidate_id = CandidateId::FromCore( canonical_candidate ).value();
    auto approvals = MultiAccountTestAccess::SecureCrdt( node_a )->ReadCandidateApprovals( candidate_id );
    ASSERT_TRUE( approvals.has_value() );
    auto approval_it = std::find_if( approvals.value().begin(), approvals.value().end(), [&]( const auto &approval )
                                    { return approval.signer == pinned_trust_address; } );
    const auto attempted = approval_it == approvals.value().end()
                             ? MultiAccountTestAccess::AttemptedTrustApproval(
                                   node_a, canonical_candidate, pinned_trust_address )
                             : *approval_it;
    const auto canonical_bytes = canonical_candidate.CanonicalBytes().value();
    ASSERT_TRUE( multisig::VerifyPayloadSignature(
        pinned_trust_address, attempted.signature, canonical_bytes ) ) << "CR-12 pinned signer mismatch";
    ASSERT_FALSE( multisig::VerifyPayloadSignature(
        replacement_address, attempted.signature, canonical_bytes ) ) << "CR-12 pinned signer mismatch";

    ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition(
        [&]
        {
            auto durable = MultiAccountTestAccess::Store( node_a )->LoadAndVerify();
            return durable.has_value() && durable.value().burn.basis_points == 100U &&
                   durable.value().burn_authorization == BurnAuthorizationKind::PeerQuorum;
        },
        std::chrono::seconds( 30 ),
        "pinned trust signer did not reach durable initial burn value 100" ) );
    EXPECT_EQ( node_a->GetAddress(), replacement_address );
}

TEST_F( PolicyLifetimeMultiAccountTest, ActiveTrustSignerSurvivesAccountSwitchAfterReadiness )
{
    auto operator_a_signer = GeniusAccount::NewFromPrivateKey( TOKEN_ID, OPERATOR_A_KEY, path_ / "after-a" );
    auto operator_b_signer = GeniusAccount::NewFromPrivateKey( TOKEN_ID, OPERATOR_B_KEY, path_ / "after-b" );
    ASSERT_TRUE( operator_a_signer );
    ASSERT_TRUE( operator_b_signer );
    const std::vector<std::string> peers{ operator_a_signer->GetAddress(), operator_b_signer->GetAddress() };

    Blockchain::SetAuthorizedFullNodeAddress( operator_a_signer->GetAddress() );
    auto node_a = NewNode( path_ / "after-a", OPERATOR_A_KEY, peers, operator_a_signer->GetAddress() );
    ASSERT_TRUE( node_a );
    const auto bootstrap_address = node_a->GetPubSub()->GetInterfaceAddress();
    auto node_b = NewNode( path_ / "after-b", OPERATOR_B_KEY, peers, operator_a_signer->GetAddress(), bootstrap_address );
    ASSERT_TRUE( node_b );
    WaitForTrustLifecycle( node_a );
    WaitForTrustLifecycle( node_b );

    GenesisManifest manifest;
    manifest.network_id              = 144;
    manifest.bootstrapper_public_key = operator_a_signer->GetAddress();
    manifest.peers                   = peers;
    manifest.membership_threshold    = 2;
    manifest.burn_threshold          = 2;
    manifest                         = manifest.Canonicalized().value();
    auto tool = SubmitReviewedGenesis( "after-tool", bootstrap_address, manifest, operator_a_signer );
    ASSERT_TRUE( tool );
    ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition(
        [&]
        {
            return node_a->GetState() == GeniusNode::NodeState::READY &&
                   node_b->GetState() == GeniusNode::NodeState::READY;
        },
        std::chrono::seconds( 50 ),
        "active members did not reach readiness" ) );

    const std::string pinned_trust_address = operator_a_signer->GetAddress();
    auto provider = MultiAccountTestAccess::Burn( node_a )->GetConfirmedValueProvider();
    ASSERT_TRUE( node_a->AddAccountWithKey( PASSIVE_C_SECONDARY_KEY ).has_value() );
    const auto accounts = node_a->GetAvailableAccounts();
    const auto replacement = std::find_if( accounts.begin(), accounts.end(), [&]( const std::string &address )
                                           { return address != pinned_trust_address; } );
    ASSERT_NE( replacement, accounts.end() );
    const std::string replacement_address = *replacement;
    ASSERT_TRUE( node_a->SelectAccount( replacement_address ).has_value() );
    ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition(
        [&] { return node_a->GetState() == GeniusNode::NodeState::READY; },
        std::chrono::seconds( 50 ),
        "replacement transaction account did not become ready" ) );

    const auto durable = MultiAccountTestAccess::Store( node_a )->LoadAndVerify().value();
    ConfirmedBurnState successor = durable.burn;
    successor.version += 1;
    successor.expected_previous_hash  = durable.burn.Hash().value();
    successor.authorizing_policy_hash = durable.policy.Hash().value();
    successor.basis_points            = 250;
    const auto canonical_candidate = BurnConfig::BurnCandidateCore( successor ).value();
    const auto expected_id = CandidateId::FromCore( canonical_candidate ).value();

    LocalTrustAdmin operator_a_admin( MultiAccountTestAccess::Registry( node_a ), MultiAccountTestAccess::Burn( node_a ) );
    auto proposed = operator_a_admin.ProposeBurn( successor.basis_points );
    const auto candidate_id = proposed.has_value() ? proposed.value() : expected_id;
    auto approvals = MultiAccountTestAccess::SecureCrdt( node_a )->ReadCandidateApprovals( candidate_id );
    ASSERT_TRUE( approvals.has_value() );
    auto approval_it = std::find_if( approvals.value().begin(), approvals.value().end(), [&]( const auto &approval )
                                    { return approval.signer == pinned_trust_address; } );
    const auto attempted = approval_it == approvals.value().end()
                             ? MultiAccountTestAccess::AttemptedTrustApproval(
                                   node_a, canonical_candidate, pinned_trust_address )
                             : *approval_it;
    const auto canonical_bytes = canonical_candidate.CanonicalBytes().value();
    ASSERT_TRUE( multisig::VerifyPayloadSignature(
        pinned_trust_address, attempted.signature, canonical_bytes ) ) << "CR-12 pinned signer mismatch";
    ASSERT_FALSE( multisig::VerifyPayloadSignature(
        replacement_address, attempted.signature, canonical_bytes ) ) << "CR-12 pinned signer mismatch";
    ASSERT_TRUE( proposed.has_value() );

    LocalTrustAdmin operator_b_admin( MultiAccountTestAccess::Registry( node_b ), MultiAccountTestAccess::Burn( node_b ) );
    ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition(
        [&]
        {
            auto replicated = MultiAccountTestAccess::SecureCrdt( node_b )->ReadCandidateApprovals( candidate_id );
            return replicated.has_value() && replicated.value().size() == 1U;
        },
        std::chrono::seconds( 20 ),
        "operator B did not retain the pinned signer's exact successor approval" ) );
    {
        auto approved = operator_b_admin.Approve( candidate_id );
        ASSERT_TRUE( approved.has_value() ) << approved.error().message();
    }
    ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition(
        [&]
        {
            auto current = MultiAccountTestAccess::Store( node_a )->LoadAndVerify();
            return current.has_value() && current.value().burn.version == successor.version &&
                   current.value().burn.basis_points == successor.basis_points;
        },
        std::chrono::seconds( 30 ),
        "exact successor did not activate under the pinned trust signer" ) );
    EXPECT_EQ( node_a->GetAddress(), replacement_address );
    EXPECT_EQ( MultiAccountTestAccess::ManagerAccountAddress( node_a ), replacement_address );
    EXPECT_EQ( MultiAccountTestAccess::ManagerProvider( MultiAccountTestAccess::Manager( node_a ) ).get(),
               provider.get() );
}

TEST_F( PolicyLifetimeMultiAccountTest, PassiveBurnSuccessorChangesPayEscrowWithoutReceiverAdmin )
{
    auto operator_a_signer = GeniusAccount::NewFromPrivateKey( TOKEN_ID, OPERATOR_A_KEY, path_ / "operator-a" );
    auto operator_b_signer = GeniusAccount::NewFromPrivateKey( TOKEN_ID, OPERATOR_B_KEY, path_ / "operator-b" );
    auto passive_c_signer = GeniusAccount::NewFromPrivateKey( TOKEN_ID, PASSIVE_C_KEY, path_ / "passive-c" );
    ASSERT_TRUE( operator_a_signer );
    ASSERT_TRUE( operator_b_signer );
    ASSERT_TRUE( passive_c_signer );
    const std::vector<std::string> peers{ operator_a_signer->GetAddress(), operator_b_signer->GetAddress() };

    Blockchain::SetAuthorizedFullNodeAddress( operator_a_signer->GetAddress() );
    auto node_a = NewNode( path_ / "operator-a", OPERATOR_A_KEY, peers, operator_a_signer->GetAddress() );
    ASSERT_TRUE( node_a );
    Blockchain::SetAuthorizedFullNodeAddress( node_a->GetAddress() );
    const auto bootstrap_address = node_a->GetPubSub()->GetInterfaceAddress();
    ASSERT_FALSE( bootstrap_address.empty() );

    auto node_b = NewNode(
        path_ / "operator-b", OPERATOR_B_KEY, peers, operator_a_signer->GetAddress(), bootstrap_address );
    auto node_c = NewNode(
        path_ / "passive-c", PASSIVE_C_KEY, peers, operator_a_signer->GetAddress(), bootstrap_address );
    ASSERT_TRUE( node_b );
    ASSERT_TRUE( node_c );
    WaitForTrustLifecycle( node_a );
    WaitForTrustLifecycle( node_b );
    WaitForTrustLifecycle( node_c );

    GenesisManifest manifest;
    manifest.network_id              = 144;
    manifest.bootstrapper_public_key = operator_a_signer->GetAddress();
    manifest.peers                   = peers;
    manifest.membership_threshold    = 2;
    manifest.burn_threshold          = 2;
    manifest                         = manifest.Canonicalized().value();
    const auto bootstrap_signature = operator_a_signer->Sign( manifest.CanonicalBytes().value() );

    const auto tool_config_path = path_ / "genesis-tool-network.json";
    const auto tool_database_path = path_ / "genesis-tool-globaldb";
    boost::filesystem::create_directories( tool_database_path );
    {
        std::ofstream config( tool_config_path.string() );
        ASSERT_TRUE( config.good() );
        config << R"({"pubsub_port":"0","pubsub_bind_address":"0.0.0.0","high_water":20,"low_water":1,"bootstrap_addresses":[")"
               << bootstrap_address << R"("]})";
    }
    crdt::GlobalDbNetworkComposition::Config tool_config;
    tool_config.network_config_path = tool_config_path.string();
    tool_config.database_path       = tool_database_path.string();
    tool_config.listen_topic        = std::string( TransactionManager::GNUS_FULL_NODES_TOPIC );
    tool_config.broadcast_topic     = std::string( TransactionManager::GNUS_FULL_NODES_TOPIC );
    auto tool_composition_result = crdt::GlobalDbNetworkComposition::Create( std::move( tool_config ) );
    ASSERT_TRUE( tool_composition_result.has_value() );
    auto tool_composition = tool_composition_result.value();
    ASSERT_TRUE( tool_composition->Start().has_value() );
    auto tool_secure = std::make_shared<securecrdt::SecureCrdt>(
        tool_composition->db(), std::string( TransactionManager::GNUS_FULL_NODES_TOPIC ) );
    auto tool_store = TrustStateStore::Open( ( path_ / "genesis-tool-trust" ).string(), manifest.network_id ).value();
    auto tool_registry = TrustedPeerRegistry::NewProduction(
        tool_secure,
        tool_store,
        manifest,
        bootstrap_signature,
        operator_a_signer->GetAddress(),
        [&]( const std::vector<uint8_t> &bytes ) { return operator_a_signer->Sign( bytes ); } ).value();
    auto tool_burn = BurnConfig::NewProduction(
        tool_secure,
        tool_registry,
        tool_store,
        operator_a_signer->GetAddress(),
        [&]( const std::vector<uint8_t> &bytes ) { return operator_a_signer->Sign( bytes ); } ).value();
    ASSERT_TRUE( tool_secure->RegisterFilters() );
    ASSERT_TRUE( tool_registry->SubmitReviewedGenesisApproval().has_value() );

    ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition(
        [&]
        {
            return node_a->GetState() == GeniusNode::NodeState::READY &&
                   node_b->GetState() == GeniusNode::NodeState::READY &&
                   node_c->GetState() == GeniusNode::NodeState::READY;
        },
        std::chrono::seconds( 50 ),
        "three production nodes did not reach durable burn-v1 readiness" ) );

    const auto initial_policy = MultiAccountTestAccess::Snapshot( node_c );
    const auto initial_manager = MultiAccountTestAccess::Manager( node_c );
    const auto initial_account = MultiAccountTestAccess::Account( node_c );
    auto provider = MultiAccountTestAccess::Burn( node_c )->GetConfirmedValueProvider();
    ASSERT_TRUE( provider->IsReady() );
    ASSERT_EQ( provider->GetBasisPoints(), 100U );
    ASSERT_EQ( MultiAccountTestAccess::ManagerProvider( initial_manager ).get(), provider.get() );

    ASSERT_TRUE( node_c->AddAccountWithKey( PASSIVE_C_SECONDARY_KEY ).has_value() );
    const auto accounts = node_c->GetAvailableAccounts();
    const auto secondary = std::find_if(
        accounts.begin(), accounts.end(), [&]( const std::string &address ) { return address != node_c->GetAddress(); } );
    ASSERT_NE( secondary, accounts.end() );
    ASSERT_TRUE( node_c->SelectAccount( *secondary ).has_value() );
    ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition(
        [&] { return node_c->GetState() == GeniusNode::NodeState::READY; },
        std::chrono::seconds( 50 ),
        "passive C replacement TransactionManager did not become ready" ) );
    const auto switched_policy = MultiAccountTestAccess::Snapshot( node_c );
    EXPECT_EQ( initial_policy.registry, switched_policy.registry );
    EXPECT_EQ( initial_policy.secure_crdt, switched_policy.secure_crdt );
    EXPECT_EQ( initial_policy.trust_store, switched_policy.trust_store );
    EXPECT_EQ( initial_policy.trusted_peer_registry, switched_policy.trusted_peer_registry );
    EXPECT_EQ( initial_policy.burn_config, switched_policy.burn_config );
    EXPECT_EQ( initial_policy.confirmed_provider, switched_policy.confirmed_provider );
    EXPECT_EQ( initial_policy.policy_registrations, switched_policy.policy_registrations );
    EXPECT_EQ( initial_policy.candidate_registrations, switched_policy.candidate_registrations );
    EXPECT_NE( initial_manager.get(), MultiAccountTestAccess::Manager( node_c ).get() );
    EXPECT_NE( initial_account, MultiAccountTestAccess::Account( node_c ) );
    EXPECT_EQ( MultiAccountTestAccess::ManagerProvider( MultiAccountTestAccess::Manager( node_c ) ).get(),
               provider.get() );

    const auto escrow_path = MultiAccountTestAccess::StoreEscrow( node_c, 10000 );
    ASSERT_FALSE( escrow_path.empty() );
    ASSERT_EQ( MultiAccountTestAccess::PayEscrowAndReadBurn( node_c, escrow_path ), 100U );
    const auto durable_v1 = MultiAccountTestAccess::Store( node_c )->LoadAndVerify().value();
    const auto durable_v1_burn_hash = durable_v1.burn.Hash().value();

    LocalTrustAdmin operator_a_admin( MultiAccountTestAccess::Registry( node_a ), MultiAccountTestAccess::Burn( node_a ) );
    LocalTrustAdmin operator_b_admin( MultiAccountTestAccess::Registry( node_b ), MultiAccountTestAccess::Burn( node_b ) );
    auto proposed = operator_a_admin.ProposeBurn( 250 );
    ASSERT_TRUE( proposed.has_value() ) << proposed.error().message();
    ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition(
        [&]
        {
            auto b_approvals = MultiAccountTestAccess::SecureCrdt( node_b )->ReadCandidateApprovals( proposed.value() );
            auto c_approvals = MultiAccountTestAccess::SecureCrdt( node_c )->ReadCandidateApprovals( proposed.value() );
            return b_approvals.has_value() && b_approvals.value().size() == 1U &&
                   c_approvals.has_value() && c_approvals.value().size() == 1U;
        },
        std::chrono::seconds( 20 ),
        "operator B and passive C did not retain operator A's burn-v2 approval" ) );
    auto approved = operator_b_admin.Approve( proposed.value() );
    ASSERT_TRUE( approved.has_value() ) << approved.error().message();

    // PASSIVE_BURN_NO_RECEIVER_ACTION_BEGIN
    ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition(
        [&]
        {
            auto durable = MultiAccountTestAccess::Store( node_c )->LoadAndVerify();
            return durable.has_value() && durable.value().burn.version == 2U &&
                   durable.value().burn.basis_points == 250U && provider->GetBasisPoints() == 250U;
        },
        std::chrono::seconds( 20 ),
        "passive C did not durably converge on burn v2" ) );
    const auto passive_c_v2 = MultiAccountTestAccess::Store( node_c )->LoadAndVerify().value();
    EXPECT_EQ( passive_c_v2.burn.version, 2U );
    EXPECT_EQ( passive_c_v2.burn.basis_points, 250U );
    EXPECT_EQ( provider->GetBasisPoints(), 250U );
    EXPECT_EQ( MultiAccountTestAccess::ManagerProvider( MultiAccountTestAccess::Manager( node_c ) ).get(),
               provider.get() );
    EXPECT_EQ( MultiAccountTestAccess::PayEscrowAndReadBurn( node_c, escrow_path ), 250U );
    // PASSIVE_BURN_NO_RECEIVER_ACTION_END

    const auto approvals = MultiAccountTestAccess::SecureCrdt( node_c )->ReadCandidateApprovals( proposed.value() );
    ASSERT_TRUE( approvals.has_value() );
    ASSERT_EQ( approvals.value().size(), 2U );
    EXPECT_EQ( std::count_if( approvals.value().begin(), approvals.value().end(), [&]( const auto &approval )
                             { return approval.signer == operator_a_signer->GetAddress(); } ),
               1 );
    EXPECT_EQ( std::count_if( approvals.value().begin(), approvals.value().end(), [&]( const auto &approval )
                             { return approval.signer == operator_b_signer->GetAddress(); } ),
               1 );
    EXPECT_EQ( std::count_if( approvals.value().begin(), approvals.value().end(), [&]( const auto &approval )
                             { return approval.signer == passive_c_signer->GetAddress(); } ),
               0 );

    ConfirmedBurnState below_quorum = passive_c_v2.burn;
    below_quorum.version += 1;
    below_quorum.expected_previous_hash = passive_c_v2.burn.Hash().value();
    below_quorum.authorizing_policy_hash = passive_c_v2.policy.Hash().value();
    below_quorum.basis_points = 333;
    const auto below_core = BurnConfig::BurnCandidateCore( below_quorum ).value();
    const auto below_id = CandidateId::FromCore( below_core ).value();
    ASSERT_TRUE( MultiAccountTestAccess::SecureCrdt( node_c )->SubmitCandidateApproval(
        { CandidateApprovalRecord::ENCODING_VERSION,
          below_core,
          operator_a_signer->GetAddress(),
          operator_a_signer->Sign( below_core.CanonicalBytes().value() ) } ).has_value() );
    ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition(
        [&]
        {
            auto retained = MultiAccountTestAccess::SecureCrdt( node_c )->ReadCandidateApprovals( below_id );
            return retained.has_value() && retained.value().size() == 1U;
        },
        std::chrono::seconds( 5 ),
        "passive C did not retain the authenticated below-quorum successor" ) );
    EXPECT_EQ( MultiAccountTestAccess::Store( node_c )->LoadAndVerify().value().burn.Hash(), passive_c_v2.burn.Hash() );
    EXPECT_EQ( provider->GetBasisPoints(), 250U );
    EXPECT_EQ( MultiAccountTestAccess::PayEscrowAndReadBurn( node_c, escrow_path ), 250U );

    ConfirmedBurnState stale = passive_c_v2.burn;
    stale.version = 2;
    stale.expected_previous_hash = durable_v1_burn_hash;
    stale.authorizing_policy_hash = passive_c_v2.policy.Hash().value();
    stale.basis_points = 444;
    const auto stale_core = BurnConfig::BurnCandidateCore( stale ).value();
    EXPECT_TRUE( MultiAccountTestAccess::SecureCrdt( node_c )->SubmitCandidateApproval(
        { CandidateApprovalRecord::ENCODING_VERSION,
          stale_core,
          operator_a_signer->GetAddress(),
          operator_a_signer->Sign( stale_core.CanonicalBytes().value() ) } ).has_error() );
    EXPECT_EQ( MultiAccountTestAccess::Store( node_c )->LoadAndVerify().value().burn.Hash(), passive_c_v2.burn.Hash() );
    EXPECT_EQ( provider->GetBasisPoints(), 250U );
    EXPECT_EQ( MultiAccountTestAccess::PayEscrowAndReadBurn( node_c, escrow_path ), 250U );

    const auto failure_config_path = path_ / "passive-c-failure-network.json";
    const auto failure_database_path = path_ / "passive-c-failure-globaldb";
    boost::filesystem::create_directories( failure_database_path );
    {
        std::ofstream config( failure_config_path.string() );
        ASSERT_TRUE( config.good() );
        config << R"({"pubsub_port":"0","pubsub_bind_address":"0.0.0.0","high_water":20,"low_water":1,"bootstrap_addresses":[")"
               << bootstrap_address << R"("]})";
    }
    crdt::GlobalDbNetworkComposition::Config failure_config;
    failure_config.network_config_path = failure_config_path.string();
    failure_config.database_path       = failure_database_path.string();
    failure_config.listen_topic        = std::string( TransactionManager::GNUS_FULL_NODES_TOPIC );
    failure_config.broadcast_topic     = std::string( TransactionManager::GNUS_FULL_NODES_TOPIC );
    auto failure_composition_result = crdt::GlobalDbNetworkComposition::Create( std::move( failure_config ) );
    ASSERT_TRUE( failure_composition_result.has_value() );
    auto failure_composition = failure_composition_result.value();
    ASSERT_TRUE( failure_composition->Start().has_value() );
    auto failure_secure = std::make_shared<securecrdt::SecureCrdt>(
        failure_composition->db(), std::string( TransactionManager::GNUS_FULL_NODES_TOPIC ) );
    std::atomic_bool fail_passive_c_commit{ false };
    std::atomic_uint32_t failed_commit_attempts{ 0 };
    auto failure_store = TrustStateStore::Open(
        ( path_ / "passive-c-failure-trust" ).string(),
        manifest.network_id,
        [&]( storage::rocksdb &database, const std::vector<TrustStateStore::Write> &writes ) -> outcome::result<void>
        {
            if ( fail_passive_c_commit.load() )
            {
                ++failed_commit_attempts;
                return outcome::failure( std::errc::io_error );
            }
            return CommitBatch( database, writes );
        } ).value();
    ASSERT_TRUE( failure_store->CommitGenesis( manifest, bootstrap_signature ).has_value() );
    const auto burn_v1_core = BurnConfig::BurnCandidateCore( durable_v1.burn ).value();
    ASSERT_TRUE( failure_store->CommitBurnSuccessor(
        durable_v1.burn, durable_v1.burn_proof, burn_v1_core.CanonicalBytes().value() ).has_value() );
    const auto burn_v2_core = BurnConfig::BurnCandidateCore( passive_c_v2.burn ).value();
    ASSERT_TRUE( failure_store->CommitBurnSuccessor(
        passive_c_v2.burn, passive_c_v2.burn_proof, burn_v2_core.CanonicalBytes().value() ).has_value() );
    std::atomic_uint32_t failure_events{ 0 };
    std::mutex failure_fields_mutex;
    std::vector<std::string> failure_fields;
    auto failure_controller = TrustStartupController::New(
        failure_secure,
        failure_store,
        manifest,
        passive_c_signer->GetAddress(),
        [&]( const std::vector<uint8_t> &bytes ) { return passive_c_signer->Sign( bytes ); },
        [&]( const TrustStartupController::Event &event )
        {
            if ( event.code == TrustStartupController::EventCode::TRUST_ACTIVATION_FAILED )
            {
                {
                    std::lock_guard<std::mutex> lock( failure_fields_mutex );
                    failure_fields = event.fields;
                }
                ++failure_events;
            }
        } ).value();
    auto failure_provider = failure_controller->burn_config()->GetConfirmedValueProvider();
    ASSERT_TRUE( failure_provider->IsReady() );
    ASSERT_EQ( failure_provider->GetBasisPoints(), 250U );
    auto failure_manager = MultiAccountTestAccess::ReplaceManager( node_c, failure_provider );
    ASSERT_TRUE( failure_manager );
    ASSERT_EQ( MultiAccountTestAccess::PayEscrowAndReadBurn( node_c, escrow_path ), 250U );

    ConfirmedBurnState failed = passive_c_v2.burn;
    failed.version += 1;
    failed.expected_previous_hash = passive_c_v2.burn.Hash().value();
    failed.authorizing_policy_hash = passive_c_v2.policy.Hash().value();
    failed.basis_points = 444;
    const auto failed_core = BurnConfig::BurnCandidateCore( failed ).value();
    const auto failed_id = CandidateId::FromCore( failed_core ).value();
    const auto failed_bytes = failed_core.CanonicalBytes().value();
    fail_passive_c_commit.store( true );
    ASSERT_TRUE( failure_secure->SubmitCandidateApproval(
        { CandidateApprovalRecord::ENCODING_VERSION,
          failed_core,
          operator_a_signer->GetAddress(),
          operator_a_signer->Sign( failed_bytes ) } ).has_value() );
    ASSERT_TRUE( failure_secure->SubmitCandidateApproval(
        { CandidateApprovalRecord::ENCODING_VERSION,
          failed_core,
          operator_b_signer->GetAddress(),
          operator_b_signer->Sign( failed_bytes ) } ).has_value() );
    ASSERT_NO_FATAL_FAILURE( test::assertWaitForCondition(
        [&] { return failure_events.load() == 1U && failed_commit_attempts.load() == 1U; },
        std::chrono::seconds( 5 ),
        "passive C commit failure was not surfaced" ) );
    {
        std::lock_guard<std::mutex> lock( failure_fields_mutex );
        ASSERT_EQ( failure_fields.size(), 4U );
        EXPECT_EQ( failure_fields.at( 0 ), failed_id.domain );
        EXPECT_EQ( failure_fields.at( 1 ), std::to_string( failed_id.version ) );
        EXPECT_EQ( failure_fields.at( 2 ), failed_id.content_hash );
        EXPECT_EQ( failure_fields.at( 3 ), "synchronous trust-state batch commit failed" );
    }
    EXPECT_EQ( failure_store->LoadAndVerify().value().burn.Hash(), passive_c_v2.burn.Hash() );
    EXPECT_EQ( failure_provider->GetBasisPoints(), 250U );
    EXPECT_EQ( MultiAccountTestAccess::PayEscrowAndReadBurn( node_c, escrow_path ), 250U );

    failure_controller.reset();
    failure_store.reset();
    failure_secure.reset();
    failure_composition->Stop();
    tool_burn.reset();
    tool_registry.reset();
    tool_secure.reset();
    tool_store.reset();
    tool_composition->Stop();
    node_c.reset();
    node_b.reset();
    node_a.reset();
}
