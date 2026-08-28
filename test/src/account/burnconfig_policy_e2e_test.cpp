#include "account/GeniusAccount.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>

#include <boost/filesystem/operations.hpp>

#include "account/BurnConfig.hpp"
#include "account/EscrowTransaction.hpp"
#include "account/GeniusSigner.hpp"
#include "account/TransactionManager.hpp"
#include "account/TransferTransaction.hpp"
#include "blockchain/Blockchain.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "securecrdt/securecrdt_test_node.hpp"
#include "storage/rocksdb/rocksdb.hpp"
#include "storage/rocksdb/rocksdb_batch.hpp"
#include "testutil/wait_condition.hpp"
#include "trustedpeer/TrustStateStore.hpp"
#include "trustedpeer/TrustedPeerRegistry.hpp"

namespace
{
    using namespace sgns;
    using namespace sgns::account;
    using namespace sgns::trustedpeer;

    class BurnConfigPolicyE2ETest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                { return std::make_shared<MemorySecureStorage>( identifier ); } );
            path_ = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
            boost::filesystem::create_directories( path_ );
            for ( size_t i = 0; i < 4; ++i ) signers_.push_back( GeniusSigner::Generate() );
            node_ = test::securecrdt::MakeSecureCrdtTestNode( "burnconfig_policy" );
            ASSERT_NE( node_, nullptr );
            secure_crdt_ = std::make_shared<securecrdt::SecureCrdt>( node_->db, "burnconfig-policy-topic" );
            store_ = TrustStateStore::Open(
                         ( path_ / "trust" ).string(),
                         42,
                         [this]( storage::rocksdb &database, const std::vector<TrustStateStore::Write> &writes )
                             -> outcome::result<void>
                         {
                             if ( fail_commits_ ) return outcome::failure( std::errc::io_error );
                             auto batch = database.batch();
                             for ( const auto &[key, value] : writes )
                             {
                                 auto put = batch->put( key, value );
                                 if ( put.has_error() ) return put.error();
                             }
                             return batch->commit();
                         } )
                         .value();
            const auto manifest = Manifest();
            tpr_ = TrustedPeerRegistry::NewProduction(
                       secure_crdt_, store_, manifest, signers_[0].Sign( manifest.CanonicalBytes().value() ),
                       signers_[0].GetAddress(),
                       [this]( const std::vector<uint8_t> &bytes ) { return signers_[0].Sign( bytes ); } )
                       .value();
            burn_ = BurnConfig::NewProduction(
                        secure_crdt_, tpr_, store_, signers_[0].GetAddress(),
                        [this]( const std::vector<uint8_t> &bytes ) { return signers_[0].Sign( bytes ); } )
                        .value();
            ASSERT_TRUE( secure_crdt_->RegisterFilters() );

            account_ = GeniusAccount::NewFromPrivateKey(
                token_id_, "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eab1", path_ / "account" );
            ASSERT_TRUE( account_ );
            ASSERT_TRUE( account_->GetUTXOManager().LoadUTXOs( node_->db->GetDataStore() ).has_value() );
            (void) account_->ConfigureDatabaseDependencies( node_->db );
            blockchain_ = Blockchain::New( node_->db, account_, node_->pubsub, []( outcome::result<void> ) {} );
            ASSERT_TRUE( blockchain_ );
            manager_ = TransactionManager::New( node_->db,
                                                node_->io,
                                                account_,
                                                blockchain_,
                                                NodeType::Light,
                                                42,
                                                std::chrono::milliseconds( 300000 ),
                                                std::chrono::milliseconds( 0 ),
                                                BurnConfig::GENESIS_DEFAULT_BASIS_POINTS,
                                                burn_->GetConfirmedValueProvider() );
            ASSERT_TRUE( manager_ );
        }

        void TearDown() override
        {
            if ( manager_ ) manager_->Stop();
            manager_.reset();
            blockchain_.reset();
            if ( account_ ) account_->DeconfigureDatabaseDependencies();
            account_.reset();
            burn_.reset();
            tpr_.reset();
            secure_crdt_.reset();
            node_.reset();
            store_.reset();
            GeniusAccount::SetSecureStorageFactory( nullptr );
            boost::filesystem::remove_all( path_ );
        }

        GenesisManifest Manifest() const
        {
            GenesisManifest manifest;
            manifest.network_id = 42;
            manifest.bootstrapper_public_key = signers_[0].GetAddress();
            manifest.peers = { signers_[2].GetAddress(), signers_[0].GetAddress(), signers_[1].GetAddress() };
            manifest.membership_threshold = 2;
            manifest.burn_threshold = 2;
            return manifest;
        }

        securecrdt::CandidateApprovalRecord Approval( const securecrdt::CandidateCore &core, size_t signer ) const
        {
            return { securecrdt::CandidateApprovalRecord::ENCODING_VERSION,
                     core,
                     signers_[signer].GetAddress(),
                     signers_[signer].Sign( core.CanonicalBytes().value() ) };
        }

        void ConfirmGenesisAndBurn()
        {
            ASSERT_TRUE( tpr_->SubmitReviewedGenesisApproval().has_value() );
            auto genesis = burn_->OnTrustedPeerGenesisConfirmed();
            ASSERT_TRUE( genesis.has_value() ) << genesis.error().message();
            auto approvals = secure_crdt_->ReadCandidateApprovals( genesis.value() ).value();
            ASSERT_EQ( approvals.size(), 1U );
            ASSERT_TRUE( secure_crdt_->SubmitCandidateApproval( Approval( approvals.front().core, 1 ) ).has_value() );
            ASSERT_TRUE( burn_->TryActivateBurnCandidate( genesis.value() ).has_value() );
            ASSERT_TRUE( burn_->IsEconomicallyReady() );
        }

        std::string StoreEscrow( uint64_t amount )
        {
            const std::string lock_id = "0x" + std::string( 64, '1' );
            SGTransaction::DAGStruct dag;
            dag.set_nonce( account_->ReserveNextNonce() );
            dag.set_source_addr( account_->GetAddress() );
            dag.set_uncle_hash( lock_id );
            dag.set_timestamp( std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::system_clock::now().time_since_epoch() )
                                   .count() );
            UTXOTxParameters params;
            params.second.push_back( { amount, lock_id, token_id_ } );
            auto escrow = std::make_shared<EscrowTransaction>(
                EscrowTransaction::New( std::move( params ), amount, std::move( dag ) ) );
            escrow->MakeSignature( *account_ );
            crdt::GlobalDB::Buffer data;
            data.put( escrow->SerializeByteVector() );
            const auto path = TransactionManager::GetTransactionPath( *escrow );
            EXPECT_TRUE( node_->db->Put( crdt::HierarchicalKey( path ), data, { account_->GetAddress() } ).has_value() );
            return path;
        }

        uint64_t PayEscrowAndReadBurn( const std::string &escrow_path )
        {
            auto transfer = PayEscrowAndReadTransfer( escrow_path );
            if ( !transfer ) return 0;
            constexpr std::string_view zero = "0x0000000000000000000000000000000000000000";
            const auto outputs = transfer->GetDstInfos();
            const auto burn = std::find_if( outputs.begin(), outputs.end(), [zero]( const OutputDestInfo &output )
                                            { return output.dest_address == zero; } );
            EXPECT_NE( burn, outputs.end() );
            return burn == outputs.end() ? 0 : burn->encrypted_amount;
        }

        std::shared_ptr<TransferTransaction> PayEscrowAndReadTransfer( const std::string &escrow_path )
        {
            SGProcessing::TaskResult result;
            auto *subtask = result.add_subtask_results();
            subtask->set_node_address( account_->GetAddress() );
            const auto &bytes = token_id_.bytes();
            subtask->set_token_id( bytes.data(), bytes.size() );
            auto paid = manager_->PayEscrow( escrow_path, result, nullptr );
            EXPECT_TRUE( paid.has_value() ) << ( paid.has_error() ? paid.error().message() : "" );
            if ( paid.has_error() ) return 0;
            std::shared_ptr<TransferTransaction> transfer;
            for ( auto bytes : manager_->GetOutTransactions() )
            {
                auto transaction = TransactionManager::DeSerializeTransaction( base::Buffer( std::move( bytes ) ) );
                if ( transaction.has_value() && transaction.value()->GetHash() == paid.value() )
                {
                    transfer = std::dynamic_pointer_cast<TransferTransaction>( transaction.value() );
                    break;
                }
            }
            EXPECT_TRUE( transfer );
            return transfer;
        }

        void ActivateBurnBasisPoints( uint64_t basis_points )
        {
            if ( burn_->GetCachedBasisPoints() == basis_points ) return;
            auto candidate = burn_->ProposeBurnCandidate( basis_points );
            ASSERT_TRUE( candidate.has_value() ) << candidate.error().message();
            auto approvals = secure_crdt_->ReadCandidateApprovals( candidate.value() );
            ASSERT_TRUE( approvals.has_value() );
            ASSERT_EQ( approvals.value().size(), 1U );
            ASSERT_TRUE(
                secure_crdt_->SubmitCandidateApproval( Approval( approvals.value().front().core, 1 ) ).has_value() );
            ASSERT_TRUE( burn_->TryActivateBurnCandidate( candidate.value() ).has_value() );
            ASSERT_EQ( burn_->GetCachedBasisPoints(), basis_points );
        }

        SGProcessing::TaskResult SingleSubtaskResult() const
        {
            SGProcessing::TaskResult result;
            auto *subtask = result.add_subtask_results();
            subtask->set_node_address( account_->GetAddress() );
            const auto &bytes = token_id_.bytes();
            subtask->set_token_id( bytes.data(), bytes.size() );
            return result;
        }

        size_t CountOutputDestinations() const
        {
            size_t count = 0;
            for ( auto bytes : manager_->GetOutTransactions() )
            {
                auto transaction = TransactionManager::DeSerializeTransaction( base::Buffer( std::move( bytes ) ) );
                if ( transaction.has_error() ) continue;
                auto transfer = std::dynamic_pointer_cast<TransferTransaction>( transaction.value() );
                if ( transfer ) count += transfer->GetDstInfos().size();
            }
            return count;
        }

        boost::filesystem::path path_;
        std::vector<GeniusSigner> signers_;
        std::unique_ptr<test::securecrdt::SecureCrdtTestNode> node_;
        std::shared_ptr<securecrdt::SecureCrdt> secure_crdt_;
        std::shared_ptr<TrustStateStore> store_;
        std::shared_ptr<TrustedPeerRegistry> tpr_;
        std::shared_ptr<BurnConfig> burn_;
        const TokenID token_id_ = TokenID::FromBytes( { 0x00 } );
        std::shared_ptr<GeniusAccount> account_;
        std::shared_ptr<Blockchain> blockchain_;
        std::shared_ptr<TransactionManager> manager_;
        bool fail_commits_ = false;
    };
}

TEST_F( BurnConfigPolicyE2ETest, GenesisWaitsForTrustedPeerConfirmationAndExactBurnQuorum )
{
    const auto pre_ready_escrow = StoreEscrow( 10000 );
    const auto transaction_count = manager_->CountTransactions();
    auto pre_ready = manager_->PayEscrow( pre_ready_escrow, SGProcessing::TaskResult{}, nullptr );
    ASSERT_TRUE( pre_ready.has_error() );
    EXPECT_EQ( pre_ready.error(), make_error_code( TransactionManager::Error::TRUST_POLICY_NOT_READY ) );
    EXPECT_EQ( manager_->CountTransactions(), transaction_count );
    EXPECT_FALSE( burn_->IsEconomicallyReady() );
    EXPECT_TRUE( burn_->ListPendingBurnCandidates().has_error() );
    EXPECT_TRUE( burn_->OnTrustedPeerGenesisConfirmed().has_error() );
    ASSERT_TRUE( tpr_->SubmitReviewedGenesisApproval().has_value() );
    auto genesis = burn_->OnTrustedPeerGenesisConfirmed();
    ASSERT_TRUE( genesis.has_value() );
    EXPECT_FALSE( burn_->IsEconomicallyReady() );
    auto approvals = secure_crdt_->ReadCandidateApprovals( genesis.value() ).value();
    ASSERT_EQ( approvals.size(), 1U );
    const auto first_hash = approvals.front().core.Hash();
    auto reordered_manifest = Manifest();
    std::reverse( reordered_manifest.peers.begin(), reordered_manifest.peers.end() );
    EXPECT_EQ( reordered_manifest.Fingerprint(), Manifest().Fingerprint() );
    auto same_burn = store_->LoadAndVerify().value().burn;
    EXPECT_EQ( BurnConfig::BurnCandidateCore( same_burn )->Hash(), first_hash );
    auto repeated = burn_->OnTrustedPeerGenesisConfirmed();
    ASSERT_TRUE( repeated.has_value() );
    EXPECT_EQ( repeated.value(), genesis.value() );
    EXPECT_EQ( secure_crdt_->ReadCandidateApprovals( genesis.value() ).value().size(), 1U );
    ASSERT_TRUE( secure_crdt_->SubmitCandidateApproval( Approval( approvals.front().core, 1 ) ).has_value() );
    ASSERT_TRUE( burn_->TryActivateBurnCandidate( genesis.value() ).has_value() );
    EXPECT_TRUE( burn_->IsEconomicallyReady() );
    EXPECT_EQ( burn_->GetCachedBasisPoints(), 100U );
    EXPECT_EQ( first_hash, approvals.front().core.Hash() );
}

TEST_F( BurnConfigPolicyE2ETest, ConfirmedSuccessorChangesActualPayEscrowBurnOutput )
{
    ConfirmGenesisAndBurn();
    const auto escrow = StoreEscrow( 10000 );
    EXPECT_EQ( PayEscrowAndReadBurn( escrow ), 100U );

    const auto old_head = store_->LoadAndVerify().value().burn.Hash();
    auto candidate = burn_->ProposeBurnCandidate( 250 ).value();
    auto core = secure_crdt_->ReadCandidateApprovals( candidate ).value().front().core;
    ASSERT_TRUE( secure_crdt_->SubmitCandidateApproval( Approval( core, 1 ) ).has_value() );
    ASSERT_TRUE( burn_->TryActivateBurnCandidate( candidate ).has_value() );
    test::assertWaitForCondition( [&] { return burn_->GetCachedBasisPoints() == 250U; },
                                  std::chrono::seconds( 5 ),
                                  "confirmed burn successor was not published" );
    EXPECT_NE( store_->LoadAndVerify().value().burn.Hash(), old_head );
    EXPECT_EQ( PayEscrowAndReadBurn( escrow ), 250U );
}

TEST_F( BurnConfigPolicyE2ETest, PolicyBindingStalesOldCandidateAndLaterVersionsNeedExplicitApproval )
{
    ConfirmGenesisAndBurn();
    const auto escrow = StoreEscrow( 10000 );
    EXPECT_EQ( PayEscrowAndReadBurn( escrow ), 100U );
    auto v2 = burn_->ProposeBurnCandidate( 250 );
    ASSERT_TRUE( v2.has_value() );
    EXPECT_EQ( secure_crdt_->ReadCandidateApprovals( v2.value() ).value().size(), 1U );
    EXPECT_TRUE( burn_->ApproveBurnCandidate( v2.value() ).has_value() );
    EXPECT_EQ( secure_crdt_->ReadCandidateApprovals( v2.value() ).value().size(), 1U );

    auto next_policy = tpr_->GetConfirmedSnapshot().value().policy;
    const auto old_policy_hash = next_policy.Hash().value();
    next_policy.version += 1;
    next_policy.expected_previous_hash = old_policy_hash;
    next_policy.authorizing_policy_hash = old_policy_hash;
    next_policy.peers = { signers_[0].GetAddress(), signers_[2].GetAddress(), signers_[3].GetAddress() };
    auto policy_id = tpr_->ProposePolicyCandidate( next_policy ).value();
    auto policy_core = TrustedPeerRegistry::PolicyCandidateCore( next_policy ).value();
    ASSERT_TRUE( secure_crdt_->SubmitCandidateApproval( Approval( policy_core, 1 ) ).has_value() );
    ASSERT_TRUE( tpr_->TryActivatePolicyCandidate( policy_id ).has_value() );

    EXPECT_TRUE( burn_->TryActivateBurnCandidate( v2.value() ).has_error() );
    EXPECT_TRUE( secure_crdt_->SubmitCandidateApproval(
        Approval( secure_crdt_->ReadCandidateApprovals( v2.value() ).value().front().core, 2 ) ).has_error() );
    auto fresh = burn_->ProposeBurnCandidate( 250 );
    ASSERT_TRUE( fresh.has_value() );
    EXPECT_NE( fresh.value().content_hash, v2.value().content_hash );
    EXPECT_EQ( secure_crdt_->ReadCandidateApprovals( fresh.value() ).value().size(), 1U );
    EXPECT_EQ( burn_->GetCachedBasisPoints(), 100U );
    EXPECT_EQ( PayEscrowAndReadBurn( escrow ), 100U );
}

TEST_F( BurnConfigPolicyE2ETest, InvalidAndUnderSignedCandidatesLeavePayEscrowUnchanged )
{
    ConfirmGenesisAndBurn();
    const auto escrow = StoreEscrow( 10000 );
    const auto durable = store_->LoadAndVerify().value();

    auto under_signed = burn_->ProposeBurnCandidate( 333 ).value();
    auto pending = burn_->TryActivateBurnCandidate( under_signed );
    ASSERT_TRUE( pending.has_value() ) << pending.error().message();
    EXPECT_FALSE( pending.value() );

    auto core = secure_crdt_->ReadCandidateApprovals( under_signed ).value().front().core;
    EXPECT_TRUE( secure_crdt_->SubmitCandidateApproval( Approval( core, 3 ) ).has_error() );

    auto wrong = durable.burn;
    wrong.version += 1;
    wrong.expected_previous_hash = std::string( 64, '0' );
    wrong.authorizing_policy_hash = durable.policy.Hash().value();
    wrong.basis_points = 444;
    const auto wrong_core = BurnConfig::BurnCandidateCore( wrong ).value();
    EXPECT_TRUE( secure_crdt_->SubmitCandidateApproval( Approval( wrong_core, 0 ) ).has_error() );

    EXPECT_EQ( store_->LoadAndVerify().value(), durable );
    EXPECT_EQ( burn_->GetCachedBasisPoints(), 100U );
    EXPECT_EQ( PayEscrowAndReadBurn( escrow ), 100U );
}

TEST_F( BurnConfigPolicyE2ETest, PersistBeforeCacheLeavesConfirmedValueAndCallbacksUnchangedOnFailure )
{
    ConfirmGenesisAndBurn();
    const auto escrow = StoreEscrow( 10000 );
    EXPECT_EQ( PayEscrowAndReadBurn( escrow ), 100U );
    uint32_t callback_count = 0;
    burn_->RegisterRefreshCallback( [&]( uint64_t ) { ++callback_count; } );
    const auto previous = burn_->GetCachedBasisPoints();
    auto candidate = burn_->ProposeBurnCandidate( 333 ).value();
    auto core = secure_crdt_->ReadCandidateApprovals( candidate ).value().front().core;
    ASSERT_TRUE( secure_crdt_->SubmitCandidateApproval( Approval( core, 1 ) ).has_value() );
    fail_commits_ = true;
    EXPECT_TRUE( burn_->TryActivateBurnCandidate( candidate ).has_error() );
    EXPECT_EQ( burn_->GetCachedBasisPoints(), previous );
    EXPECT_TRUE( burn_->IsEconomicallyReady() );
    EXPECT_EQ( callback_count, 0U );
    EXPECT_EQ( PayEscrowAndReadBurn( escrow ), 100U );
}

TEST_F( BurnConfigPolicyE2ETest, PayEscrowUsesExactOverflowSafeBurnForUint64Maximum )
{
    ConfirmGenesisAndBurn();
    constexpr auto maximum = std::numeric_limits<uint64_t>::max();

    ActivateBurnBasisPoints( 1 );
    EXPECT_EQ( PayEscrowAndReadBurn( StoreEscrow( maximum ) ), 1844674407370955ULL );

    ActivateBurnBasisPoints( 100 );
    EXPECT_EQ( PayEscrowAndReadBurn( StoreEscrow( maximum ) ), 184467440737095516ULL );

    ActivateBurnBasisPoints( TransactionManager::BASIS_POINTS_TOTAL );
    auto full_burn = PayEscrowAndReadTransfer( StoreEscrow( maximum ) );
    ASSERT_TRUE( full_burn );
    constexpr std::string_view zero = "0x0000000000000000000000000000000000000000";
    const auto outputs = full_burn->GetDstInfos();
    const auto burn = std::find_if( outputs.begin(), outputs.end(), [zero]( const OutputDestInfo &output )
                                    { return output.dest_address == zero; } );
    ASSERT_NE( burn, outputs.end() );
    EXPECT_EQ( burn->encrypted_amount, maximum );
    for ( const auto &output : outputs )
    {
        if ( output.dest_address != zero ) EXPECT_EQ( output.encrypted_amount, 0U );
    }

    manager_->Stop();
    manager_ = TransactionManager::New( node_->db,
                                        node_->io,
                                        account_,
                                        blockchain_,
                                        NodeType::Light,
                                        42,
                                        std::chrono::milliseconds( 300000 ),
                                        std::chrono::milliseconds( 0 ),
                                        TransactionManager::BASIS_POINTS_TOTAL + 1,
                                        nullptr );
    ASSERT_TRUE( manager_ );
    const auto invalid_escrow = StoreEscrow( maximum );
    const auto transaction_count = manager_->CountTransactions();
    const auto outgoing_count = manager_->GetOutTransactions().size();
    const auto output_count = CountOutputDestinations();
    auto invalid = manager_->PayEscrow( invalid_escrow, SingleSubtaskResult(), nullptr );
    ASSERT_TRUE( invalid.has_error() );
    EXPECT_EQ( invalid.error(), std::make_error_code( std::errc::invalid_argument ) );
    EXPECT_EQ( manager_->CountTransactions(), transaction_count );
    EXPECT_EQ( manager_->GetOutTransactions().size(), outgoing_count );
    EXPECT_EQ( CountOutputDestinations(), output_count );
}
