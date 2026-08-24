/**
 * @file       transaction_manager_certificate_fallback_test.cpp
 * @brief      Tests for certificate fallback deserialization in OnConsensusCertificate.
 * @details    Verifies CONFLICT-01 and NONCE-01: standalone validators process
 *             certificate-embedded transactions into local state, enabling
 *             double-spend and nonce-replay detection via existing infrastructure.
 * @date       2026-05-28
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#include <gtest/gtest.h>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include <boost/filesystem/operations.hpp>

#include "account/TransactionManager.hpp"
#include "account/TransferTransaction.hpp"
#include "account/MintTransactionV2.hpp"
#include "account/GeniusAccount.hpp"
#include "blockchain/Blockchain.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"
#include "account/proto/SGTransaction.pb.h"
#include "crypto/hasher.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include <gsl/span>
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/wait_condition.hpp"

using namespace sgns;

namespace sgns
{
    /**
     * @brief Friend accessor for private TransactionManager methods.
     *        Declared as a friend in TransactionManager.hpp so it can access
     *        OnConsensusCertificate, GetTransactionByHash, and GetTrackedTxByHash.
     */
    class CertificateFallbackTestAccess
    {
    public:
        static outcome::result<std::shared_ptr<GeniusTransaction>> DeSerializeEmbeddedTransaction(
            TransactionManager        &tm,
            const EmbeddedTransaction &embedded )
        {
            return tm.DeSerializeEmbeddedTransaction( embedded );
        }

        static outcome::result<ConsensusManager::Check> OnConsensusCertificate( TransactionManager         &tm,
                                                                                const std::string          &tx_hash,
                                                                                const ConsensusCertificate &cert )
        {
            return tm.OnConsensusCertificate( tx_hash, cert );
        }

        static std::shared_ptr<GeniusTransaction> GetTransactionByHash( TransactionManager &tm,
                                                                        const std::string  &hash )
        {
            return tm.GetTransactionByHash( hash );
        }

        static std::optional<TransactionManager::TrackedTx> GetTrackedTxByHash( TransactionManager &tm,
                                                                                const std::string  &hash )
        {
            return tm.GetTrackedTxByHash( hash );
        }

        static ConsensusManager::Check EvaluateReplayProtection( TransactionManager      &tm,
                                                                 const GeniusTransaction &transaction )
        {
            return tm.EvaluateTransactionReplayProtection( transaction ).validation.check;
        }

        static outcome::result<void> FetchAndProcessTransaction( TransactionManager         &tm,
                                                                 const std::string          &key,
                                                                 std::optional<base::Buffer> data )
        {
            return tm.FetchAndProcessTransaction( key, std::move( data ) );
        }

        static outcome::result<std::optional<std::shared_ptr<GeniusTransaction>>> FetchExactTransactionFromCRDT(
            TransactionManager &tm,
            const std::string  &tx_hash )
        {
            return tm.FetchExactTransactionFromCRDT( tx_hash );
        }

        static std::shared_ptr<ConsensusManager> ConsensusManagerOf( Blockchain &blockchain )
        {
            return blockchain.consensus_manager_;
        }

        static void CertificateReceived( const std::shared_ptr<ConsensusManager> &manager,
                                         crdt::CRDTCallbackManager::NewDataPair   new_data )
        {
            manager->CertificateReceived( std::move( new_data ), std::string{} );
        }

        static void RecoverPendingCertificateWork( const std::shared_ptr<ConsensusManager> &manager )
        {
            manager->RecoverPendingCertificateWork();
        }

        static bool HasCertificateWorkState( const std::shared_ptr<ConsensusManager> &manager,
                                             const std::string                       &key,
                                             crdt::CRDTWorkJournal::State             state )
        {
            const auto entry = manager->certificate_work_journal_->GetEntry( key );
            return entry.has_value() && entry->state == state;
        }

        static bool HasNoCertificateWork( const std::shared_ptr<ConsensusManager> &manager, const std::string &key )
        {
            return !manager->certificate_work_journal_->GetEntry( key ).has_value();
        }

        static std::string GetExpectedCertificateSlotKey( const ConsensusCertificate &certificate )
        {
            return ConsensusManager::GetExpectedCertificateSlotKey( certificate );
        }

        static void SetBridgeExecutedMarkerWriteFailure( TransactionManager &tm, bool fail )
        {
            tm.SetBridgeExecutedMarkerWriteFailureForTest( fail );
        }

        static void SetFetchAndProcessBeforeStateChangeHook( TransactionManager &tm, std::function<void()> hook )
        {
            tm.SetFetchAndProcessBeforeStateChangeHookForTest( std::move( hook ) );
        }

        static void SetFailNextPutUTXOStore( UTXOManager &utxo_manager, bool fail )
        {
            utxo_manager.SetFailNextPutUTXOStoreForTest( fail );
        }

        static void SetPutUTXOBeforeStoreHook( UTXOManager &utxo_manager, std::function<void()> hook )
        {
            utxo_manager.SetPutUTXOBeforeStoreHookForTest( std::move( hook ) );
        }
    };
} // namespace sgns

namespace
{
    /// @brief Test token identifier.
    const sgns::TokenID kTestTokenId = sgns::TokenID::FromBytes( { 0x00 } );

    /// @brief Default proposer ID used in test certificates.
    constexpr const char *kTestProposer = "test-proposer";

    /**
     * @brief Builds a ConsensusCertificate proto wrapping the given subject.
     * @param[in] subject      ConsensusSubject to embed in the proposal.
     * @param[in] proposal_id  Unique proposal identifier.
     * @return Populated ConsensusCertificate.
     */
    ConsensusCertificate BuildCertificate( const ConsensusManager::Subject &subject, const std::string &proposal_id )
    {
        ConsensusCertificate cert;
        cert.set_proposal_id( proposal_id );
        const auto now_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );
        cert.set_timestamp( now_ms );
        cert.set_total_weight( 1 );
        cert.set_approved_weight( 1 );

        auto *proposal = cert.mutable_proposal();
        proposal->set_proposal_id( proposal_id );
        proposal->set_proposer_id( kTestProposer );
        proposal->set_timestamp( now_ms );
        *proposal->mutable_subject() = subject;

        return cert;
    }

    /**
     * @brief Creates a minimal EmbeddedTransaction::kTransfer with a valid data_hash.
     * @details First deserializes a bare TransferTx to get a real TransferTransaction
     *          object, then uses its SerializeByteVector method to compute the correct
     *          hash (matching CheckHash's verification path), and finally rebuilds the
     *          EmbeddedTransaction with the correct data_hash set.
     * @param[in] tm TransactionManager used for deserialization.
     * @return EmbeddedTransaction with a properly hashed TransferTx.
     */
    EmbeddedTransaction MakeMinimalEmbeddedTransfer( TransactionManager &tm,
                                                     const std::string  &source_address,
                                                     uint64_t            nonce )
    {
        // Step 1: Create a bare TransferTx proto (no data_hash)
        SGTransaction::TransferTx bare_tx;
        bare_tx.mutable_dag_struct()->set_type( "transfer" );
        bare_tx.mutable_dag_struct()->set_source_addr( source_address );
        bare_tx.mutable_dag_struct()->set_nonce( nonce );

        EmbeddedTransaction bare_embedded;
        *bare_embedded.mutable_transfer() = bare_tx;

        // Step 2: Deserialize to get a real TransferTransaction object
        auto deser_result = CertificateFallbackTestAccess::DeSerializeEmbeddedTransaction( tm, bare_embedded );
        assert( deser_result.has_value() && deser_result.value() != nullptr );
        auto tx_obj = deser_result.value();

        // Step 3: Compute hash using SerializeByteVector (matching CheckHash path)
        SGTransaction::DAGStruct dag_copy = tx_obj->dag_st;
        dag_copy.clear_signature();
        dag_copy.clear_data_hash();
        auto serialized_bytes = tx_obj->SerializeByteVector( dag_copy );
        auto hash             = sgns::crypto::blake2b_256(
            gsl::span<const uint8_t>( serialized_bytes.data(), serialized_bytes.size() ) );

        // Step 4: Rebuild the TransferTx proto with the correct data_hash
        SGTransaction::TransferTx final_tx;
        *final_tx.mutable_dag_struct() = bare_tx.dag_struct();
        final_tx.mutable_dag_struct()->set_data_hash( hash.toReadableString() );

        EmbeddedTransaction embedded;
        *embedded.mutable_transfer() = final_tx;
        return embedded;
    }

    /**
     * @brief Deserializes a TransferTx embedded transaction and returns its hash.
     * @details Uses the same deserialization path as DeSerializeEmbeddedTransaction
     *          to guarantee hash consistency with the OnConsensusCertificate code path.
     * @param[in] embedded EmbeddedTransaction with kTransfer case set.
     * @return Transaction hash string, or empty string on failure.
     */
    std::string ComputeEmbeddedTxHash( TransactionManager &tm, const EmbeddedTransaction &embedded )
    {
        auto tx_result = CertificateFallbackTestAccess::DeSerializeEmbeddedTransaction( tm, embedded );
        if ( tx_result.has_error() )
        {
            return {};
        }
        return tx_result.value()->GetHash();
    }

    /**
     * @brief Convenience: builds a NonceSubject as a ConsensusSubject proto.
     * @param[in] account_id Account bound to the subject.
     * @param[in] nonce      Account nonce.
     * @param[in] tx_hash    Transaction hash.
     * @param[in] embedded   EmbeddedTransaction payload.
     * @return ConsensusSubject wrapping the NonceSubject.
     */
    ConsensusManager::Subject MakeNonceSubject( const std::string         &account_id,
                                                uint64_t                   nonce,
                                                const std::string         &tx_hash,
                                                const EmbeddedTransaction &embedded )
    {
        auto result = ConsensusManager::CreateNonceSubject( account_id,
                                                            nonce,
                                                            tx_hash,
                                                            embedded,
                                                            std::nullopt,
                                                            std::nullopt );
        return result.value();
    }

    std::shared_ptr<TransferTransaction> MakeTransfer( const std::string &source_address,
                                                       uint64_t           nonce,
                                                       const std::string &previous_hash = {} )
    {
        SGTransaction::DAGStruct dag;
        dag.set_type( "transfer" );
        dag.set_source_addr( source_address );
        dag.set_nonce( nonce );
        dag.set_previous_hash( previous_hash );
        dag.set_timestamp( static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() ) );
        return std::make_shared<TransferTransaction>( TransferTransaction::New( {}, {}, std::move( dag ) ) );
    }

    std::shared_ptr<MintTransactionV2> MakeCompetingMintV2( const std::string &source_address, uint64_t nonce )
    {
        SGTransaction::DAGStruct dag;
        dag.set_type( "mint-v2" );
        dag.set_source_addr( source_address );
        dag.set_nonce( nonce );
        dag.set_timestamp( static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() ) );

        const auto burn_hash = base::Hash256::fromReadableString( std::string( 64, 'a' ) );
        assert( burn_hash.has_value() );
        return std::make_shared<MintTransactionV2>( MintTransactionV2::New( 42,
                                                                            "source-chain",
                                                                            kTestTokenId,
                                                                            std::move( dag ),
                                                                            { { burn_hash.value(), 0, {} } },
                                                                            source_address ) );
    }
} // anonymous namespace

/**
 * @brief Lightweight test fixture using CRDTFixture for database/pubsub and
 *        creating a TransactionManager directly (no GeniusNode, no network sync).
 */
class CertificateFallbackTest : public ::test::CRDTFixture
{
public:
    CertificateFallbackTest() : ::test::CRDTFixture( "cert_fallback_test" )
    {
    }

    void SetUp() override
    {
        GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                                                { return std::make_shared<MemorySecureStorage>( identifier ); } );

        // Create a GeniusAccount for the TransactionManager (random key, no crypto derivation)
        account_ = GeniusAccount::New( kTestTokenId, base_path / "account" );
        assert( account_ != nullptr );

        // Load the UTXOManager's DB so ParseTransaction can store UTXOs
        auto load_result = account_->GetUTXOManager().LoadUTXOs( db_->GetDataStore() );
        assert( load_result.has_value() );

        // Create a Blockchain with a no-op callback
        blockchain_ = Blockchain::New( db_, account_, pubs_, []( outcome::result<void> ) {} );
        assert( blockchain_ != nullptr );

        // Create a TransactionManager in non-full-node mode
        constexpr auto kTimestampTolerance = std::chrono::milliseconds( 300000 );
        constexpr auto kMutabilityWindow   = std::chrono::milliseconds( 600000 );

        tm_ = TransactionManager::New( db_,
                                       io_,
                                       account_,
                                       blockchain_,
                                       false, // full_node
                                       0,     // subnet_id
                                       kTimestampTolerance,
                                       kMutabilityWindow );
        assert( tm_ != nullptr );
    }

    ~CertificateFallbackTest() override = default;

    std::shared_ptr<GeniusAccount>      account_;
    std::shared_ptr<Blockchain>         blockchain_;
    std::shared_ptr<TransactionManager> tm_;

    void PersistTransaction( const std::shared_ptr<GeniusTransaction> &transaction )
    {
        ASSERT_TRUE( transaction );
        crdt::GlobalDB::Buffer serialized;
        serialized.put( transaction->SerializeByteVector() );
        ASSERT_TRUE(
            db_->Put( { TransactionManager::GetTransactionPath( *transaction ) }, serialized, {} ).has_value() );
    }

    void PersistLegacyCertificateRecord( const std::string &transaction_hash )
    {
        crdt::GlobalDB::Buffer legacy_value;
        legacy_value.put( "legacy-certificate-record" );
        ASSERT_TRUE( db_->Put( { "/cert/" + transaction_hash }, legacy_value, {} ).has_value() );
    }

    outcome::result<ConsensusCertificate> BuildSignedCertificate(
        const std::shared_ptr<GeniusTransaction> &transaction )
    {
        if ( !transaction )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        auto registry = blockchain_->GetValidatorRegistry();
        if ( !registry || registry
                              ->StoreGenesisRegistry( account_->GetAddress(),
                                                      [account = account_]( std::vector<uint8_t> payload )
                                                      { return account->Sign( std::move( payload ) ); } )
                              .has_error() )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        if ( !::waitForCondition(
                 [&registry]()
                 {
                     auto current = registry->LoadCurrentRegistry();
                     return current.has_value() && !registry->GetRegistryCid().empty();
                 },
                 std::chrono::milliseconds( 2000 ),
                 nullptr ) )
        {
            return outcome::failure( std::errc::timed_out );
        }

        auto signing_manager = ConsensusManager::New(
            registry,
            db_,
            pubs_,
            [account = account_]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); },
            account_->GetAddress() );
        if ( !signing_manager )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        const auto subject = ConsensusManager::CreateNonceSubject( account_->GetAddress(),
                                                                   transaction->GetNonce(),
                                                                   transaction->GetHash(),
                                                                   transaction->SerializeToEmbeddedTransaction(),
                                                                   std::nullopt,
                                                                   std::nullopt );
        if ( subject.has_error() )
        {
            signing_manager->Close();
            return outcome::failure( subject.error() );
        }
        const auto proposal = signing_manager->CreateProposal( subject.value(),
                                                               account_->GetAddress(),
                                                               registry->GetRegistryCid(),
                                                               registry->GetRegistryEpoch() );
        if ( proposal.has_error() )
        {
            signing_manager->Close();
            return outcome::failure( proposal.error() );
        }
        const auto vote = signing_manager->CreateVote( proposal.value().proposal_id(),
                                                       account_->GetAddress(),
                                                       true,
                                                       [account = account_]( std::vector<uint8_t> payload )
                                                       { return account->Sign( std::move( payload ) ); } );
        if ( vote.has_error() )
        {
            signing_manager->Close();
            return outcome::failure( vote.error() );
        }
        const auto certificate = signing_manager->CreateCertificate( proposal.value(), { vote.value() } );
        if ( certificate.has_error() )
        {
            signing_manager->Close();
            return outcome::failure( certificate.error() );
        }
        signing_manager->Close();
        return certificate.value();
    }

    void PersistCertificateAtSlot( const std::string &slot, const ConsensusCertificate &certificate )
    {
        std::string serialized;
        ASSERT_TRUE( certificate.SerializeToString( &serialized ) );
        crdt::GlobalDB::Buffer value;
        value.put( serialized );
        ASSERT_TRUE( db_->Put( { "/cert/" + slot }, value, {} ).has_value() );
    }

    outcome::result<void> FetchAndProcess( const std::shared_ptr<GeniusTransaction> &transaction )
    {
        base::Buffer serialized;
        serialized.put( transaction->SerializeByteVector() );
        return CertificateFallbackTestAccess::FetchAndProcessTransaction(
            *tm_,
            TransactionManager::GetTransactionPath( *transaction ),
            std::move( serialized ) );
    }
};

// ---------------------------------------------------------------------------
// Happy-path tests
// ---------------------------------------------------------------------------

/**
 * CONFLICT-01 / D-01/D-02/D-03: Certificate with NonceSubject containing a valid
 * embedded TransferTransaction. OnConsensusCertificate enters the fallback path
 * (GetTransactionByHash returns null), deserializes from the certificate, and
 * returns Check::Approve.
 */
TEST_F( CertificateFallbackTest, HappyPath_ValidEmbeddedTx_ReturnsApprove )
{
    const auto        embedded = MakeMinimalEmbeddedTransfer( *tm_, account_->GetAddress(), 1 );
    const std::string tx_hash  = ComputeEmbeddedTxHash( *tm_, embedded );
    ASSERT_FALSE( tx_hash.empty() );

    const auto subject = MakeNonceSubject( account_->GetAddress(), 1, tx_hash, embedded );
    const auto cert    = BuildCertificate( subject, "proposal-happy-01" );

    const auto result = CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, tx_hash, cert );
    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( result.value(), ConsensusManager::Check::Approve );
}

/**
 * CONFLICT-01 / D-03: After certificate fallback processing, GetTransactionByHash
 * returns a non-null entry for the deserialized tx.
 */
TEST_F( CertificateFallbackTest, HappyPath_TxStoredAfterFallback )
{
    const auto        embedded = MakeMinimalEmbeddedTransfer( *tm_, account_->GetAddress(), 2 );
    const std::string tx_hash  = ComputeEmbeddedTxHash( *tm_, embedded );
    ASSERT_FALSE( tx_hash.empty() );

    const auto subject = MakeNonceSubject( account_->GetAddress(), 2, tx_hash, embedded );
    const auto cert    = BuildCertificate( subject, "proposal-stored-01" );

    // Before: tx is not in the local store
    EXPECT_EQ( CertificateFallbackTestAccess::GetTransactionByHash( *tm_, tx_hash ), nullptr );

    const auto result = CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, tx_hash, cert );
    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( result.value(), ConsensusManager::Check::Approve );

    // After: tx is now in the local store
    const auto stored_tx = CertificateFallbackTestAccess::GetTransactionByHash( *tm_, tx_hash );
    EXPECT_NE( stored_tx, nullptr );
}

/**
 * CONFLICT-01 / D-03: After certificate fallback processing, the stored
 * TrackedTx has CONFIRMED status (populates tx_processed_m for future
 * HasConfirmedInputConflict checks).
 */
TEST_F( CertificateFallbackTest, HappyPath_TrackedTxIsConfirmed )
{
    const auto        embedded = MakeMinimalEmbeddedTransfer( *tm_, account_->GetAddress(), 3 );
    const std::string tx_hash  = ComputeEmbeddedTxHash( *tm_, embedded );
    ASSERT_FALSE( tx_hash.empty() );

    const auto subject = MakeNonceSubject( account_->GetAddress(), 3, tx_hash, embedded );
    const auto cert    = BuildCertificate( subject, "proposal-confirmed-01" );

    const auto result = CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, tx_hash, cert );
    ASSERT_TRUE( result.has_value() );

    const auto tracked = CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, tx_hash );
    ASSERT_TRUE( tracked.has_value() );
    EXPECT_EQ( tracked->status, TransactionManager::TransactionStatus::CONFIRMED );
}

// ---------------------------------------------------------------------------
// Edge-case tests (Task 3)
// ---------------------------------------------------------------------------

/**
 * Edge case 1: Certificate with empty EmbeddedTransaction (TRANSACTION_NOT_SET).
 * This represents a pre-Phase-1 certificate. The code must return
 * Check::Approve without attempting deserialization.
 */
TEST_F( CertificateFallbackTest, EdgeCase_EmptyEmbeddedTransaction_ReturnsApprove )
{
    const auto subject = ConsensusManager::CreateNonceSubject( account_->GetAddress(),
                                                               10,
                                                               "fake-hash-empty",
                                                               EmbeddedTransaction{},
                                                               std::nullopt,
                                                               std::nullopt )
                             .value();
    const auto cert = BuildCertificate( subject, "proposal-empty-01" );

    const auto result = CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, "fake-hash-empty", cert );
    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( result.value(), ConsensusManager::Check::Approve );
}

/**
 * Edge case 2: Certificate whose subject is not a NonceSubject (e.g., a
 * generic subject with a different type hash). DecodeNonceSubject fails,
 * code returns Check::Approve.
 */
TEST_F( CertificateFallbackTest, EdgeCase_NonNonceSubject_ReturnsApprove )
{
    const std::vector<uint8_t> payload = { 0x01, 0x02, 0x03 };
    const auto                 subject =
        ConsensusManager::CreateGenericSubject( account_->GetAddress(), "gnus.bridge_event.v1", payload ).value();
    const auto cert = BuildCertificate( subject, "proposal-generic-01" );

    const auto result = CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, "fake-hash-generic", cert );
    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( result.value(), ConsensusManager::Check::Approve );
}

/**
 * Edge case 3: Certificate where the tx_hash parameter does not match the
 * hash of the deserialized embedded transaction. The defensive hash integrity
 * check (tx->GetHash() != tx_hash) triggers, returning Check::Approve
 * without processing the embedded data.
 */
TEST_F( CertificateFallbackTest, EdgeCase_HashMismatch_ReturnsApprove )
{
    const auto        embedded        = MakeMinimalEmbeddedTransfer( *tm_, account_->GetAddress(), 11 );
    const std::string real_hash       = ComputeEmbeddedTxHash( *tm_, embedded );
    const std::string mismatched_hash = "definitely-not-the-real-hash-value";

    const auto subject = MakeNonceSubject( account_->GetAddress(), 11, mismatched_hash, embedded );
    const auto cert    = BuildCertificate( subject, "proposal-mismatch-01" );

    const auto result = CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, mismatched_hash, cert );
    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( result.value(), ConsensusManager::Check::Approve );

    // Verify: the real tx was NOT stored (hash gate prevented processing)
    EXPECT_EQ( CertificateFallbackTestAccess::GetTransactionByHash( *tm_, real_hash ), nullptr );
}

/**
 * Edge case 4: Certificate where the tx_hash in the NonceSubject differs from
 * the tx_hash parameter passed to OnConsensusCertificate. GetTransactionByHash
 * returns null (no local tx with the parameter hash), but the deserialized tx's
 * hash doesn't match the parameter either -> hash mismatch -> Approve.
 */
TEST_F( CertificateFallbackTest, EdgeCase_ParameterHashDiffersFromSubject_ReturnsApprove )
{
    const auto        embedded  = MakeMinimalEmbeddedTransfer( *tm_, account_->GetAddress(), 12 );
    const std::string real_hash = ComputeEmbeddedTxHash( *tm_, embedded );
    ASSERT_FALSE( real_hash.empty() );

    const auto subject = MakeNonceSubject( account_->GetAddress(), 12, real_hash, embedded );
    const auto cert    = BuildCertificate( subject, "proposal-param-diff-01" );

    const std::string wrong_param_hash = "some-other-hash-not-in-store";
    const auto        result = CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, wrong_param_hash, cert );
    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( result.value(), ConsensusManager::Check::Approve );
}

/**
 * Regression: Certificate arrives for a tx already in tx_processed_m.
 * The existing path runs (GetTransactionByHash returns non-null),
 * promoting VERIFYING -> CONFIRMED. Returns Check::Approve.
 */
TEST_F( CertificateFallbackTest, Regression_TxAlreadyInStore_ExistingPathApproves )
{
    const auto        embedded = MakeMinimalEmbeddedTransfer( *tm_, account_->GetAddress(), 20 );
    const std::string tx_hash  = ComputeEmbeddedTxHash( *tm_, embedded );
    ASSERT_FALSE( tx_hash.empty() );

    // First certificate: enters fallback path, stores the tx
    const auto subject1 = MakeNonceSubject( account_->GetAddress(), 20, tx_hash, embedded );
    const auto cert1    = BuildCertificate( subject1, "proposal-regression-first" );

    const auto result1 = CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, tx_hash, cert1 );
    ASSERT_TRUE( result1.has_value() );
    EXPECT_EQ( result1.value(), ConsensusManager::Check::Approve );

    // Verify: tx is now in the store
    EXPECT_NE( CertificateFallbackTestAccess::GetTransactionByHash( *tm_, tx_hash ), nullptr );

    // Second certificate for the same tx: existing path (GetTransactionByHash returns non-null)
    const auto subject2 = MakeNonceSubject( account_->GetAddress(), 20, tx_hash, embedded );
    const auto cert2    = BuildCertificate( subject2, "proposal-regression-second" );

    const auto result2 = CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, tx_hash, cert2 );
    ASSERT_TRUE( result2.has_value() );
    EXPECT_EQ( result2.value(), ConsensusManager::Check::Approve );
}

/**
 * Multiple certificates for the same tx: idempotent behavior.
 * The tx is stored once and remains in the store after repeated certs.
 */
TEST_F( CertificateFallbackTest, MultipleCerts_SameTx_Idempotent )
{
    const auto        embedded = MakeMinimalEmbeddedTransfer( *tm_, account_->GetAddress(), 30 );
    const std::string tx_hash  = ComputeEmbeddedTxHash( *tm_, embedded );
    ASSERT_FALSE( tx_hash.empty() );

    // First cert: stores the tx
    const auto subject_a = MakeNonceSubject( account_->GetAddress(), 30, tx_hash, embedded );
    const auto cert_a    = BuildCertificate( subject_a, "proposal-multi-a" );

    const auto result_a = CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, tx_hash, cert_a );
    ASSERT_TRUE( result_a.has_value() );
    EXPECT_EQ( result_a.value(), ConsensusManager::Check::Approve );
    EXPECT_NE( CertificateFallbackTestAccess::GetTransactionByHash( *tm_, tx_hash ), nullptr );

    // Second cert with same tx (idempotent -- already stored from first cert)
    const auto subject_b = MakeNonceSubject( account_->GetAddress(), 30, tx_hash, embedded );
    const auto cert_b    = BuildCertificate( subject_b, "proposal-multi-b" );

    const auto result_b = CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, tx_hash, cert_b );
    ASSERT_TRUE( result_b.has_value() );
    EXPECT_EQ( result_b.value(), ConsensusManager::Check::Approve );

    // Tx still in store (not duplicated or corrupted by second cert)
    EXPECT_NE( CertificateFallbackTestAccess::GetTransactionByHash( *tm_, tx_hash ), nullptr );
}

/**
 * A hash-only prior transaction dependency remains pending when the producer
 * transaction has not been recovered from CRDT. A certificate hash record
 * alone is never a finality authority key.
 */
TEST_F( CertificateFallbackTest, ReplayProtection_MissingPreviousTransactionRemainsPending )
{
    const auto previous_hash = std::string( "missing-previous-transaction" );
    const auto candidate     = MakeTransfer( account_->GetAddress(), 1, previous_hash );

    PersistLegacyCertificateRecord( previous_hash );

    EXPECT_EQ( CertificateFallbackTestAccess::EvaluateReplayProtection( *tm_, *candidate ),
               ConsensusManager::Check::Pending );
}

/**
 * Even after the previous transaction is available in CRDT, only its derived
 * slot can establish finality. A legacy /cert/<transaction-hash> record must
 * leave the nonce dependency pending when no authoritative slot record exists.
 */
TEST_F( CertificateFallbackTest, ReplayProtection_RejectsLegacyHashCertificateRecord )
{
    const auto previous = MakeTransfer( account_->GetAddress(), 0 );
    ASSERT_FALSE( previous->GetHash().empty() );
    PersistTransaction( previous );
    PersistLegacyCertificateRecord( previous->GetHash() );

    const auto candidate = MakeTransfer( account_->GetAddress(), 1, previous->GetHash() );

    EXPECT_EQ( CertificateFallbackTestAccess::EvaluateReplayProtection( *tm_, *candidate ),
               ConsensusManager::Check::Pending );
}

/**
 * A Mint V2 burn identifies a shared canonical slot even when separate
 * proposers create distinct transaction envelopes. A valid, signed certificate
 * for the winning transaction must not confirm the loser merely because both
 * derive that same slot.
 */
TEST_F( CertificateFallbackTest, SharedMintSlotConfirmsOnlyTheCertifiedTransaction )
{
    const auto winner = MakeCompetingMintV2( account_->GetAddress(), 70 );
    const auto loser  = MakeCompetingMintV2( account_->GetAddress(), 71 );
    ASSERT_NE( winner->GetHash(), loser->GetHash() );
    ASSERT_EQ( winner->GetSlotID(), loser->GetSlotID() );

    const auto certificate = BuildSignedCertificate( winner );
    ASSERT_TRUE( certificate.has_value() );
    PersistCertificateAtSlot( winner->GetSlotID(), certificate.value() );

    // The Blockchain-side lookup validates the persisted authoritative record.
    const auto loaded = blockchain_->GetCertificateBySlot( winner->GetSlotID() );
    ASSERT_TRUE( loaded.has_value() );
    EXPECT_TRUE( TransactionManager::CertificateMatchesTransaction( loaded.value(), *winner ) );
    EXPECT_FALSE( TransactionManager::CertificateMatchesTransaction( loaded.value(), *loser ) );

    ASSERT_TRUE( FetchAndProcess( winner ).has_value() );
    ASSERT_TRUE( FetchAndProcess( loser ).has_value() );

    const auto winner_tracked = CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, winner->GetHash() );
    const auto loser_tracked  = CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, loser->GetHash() );
    ASSERT_TRUE( winner_tracked.has_value() );
    ASSERT_TRUE( loser_tracked.has_value() );
    EXPECT_EQ( winner_tracked->status, TransactionManager::TransactionStatus::CONFIRMED );
    EXPECT_EQ( loser_tracked->status, TransactionManager::TransactionStatus::VERIFYING );
}

/**
 * Certificate-first delivery must recover the exact winner from its normal
 * CRDT transaction path before considering the certificate-embedded copy.
 */
TEST_F( CertificateFallbackTest, CertificateFirstRecoversExactWinnerFromCRDT )
{
    const auto winner = MakeCompetingMintV2( account_->GetAddress(), 90 );
    ASSERT_FALSE( winner->GetHash().empty() );
    PersistTransaction( winner );

    EXPECT_EQ( CertificateFallbackTestAccess::GetTransactionByHash( *tm_, winner->GetHash() ), nullptr );

    const auto recovered = CertificateFallbackTestAccess::FetchExactTransactionFromCRDT( *tm_, winner->GetHash() );
    ASSERT_TRUE( recovered.has_value() );
    ASSERT_TRUE( recovered.value().has_value() );
    EXPECT_EQ( recovered.value().value()->GetHash(), winner->GetHash() );

    const auto certificate = BuildSignedCertificate( winner );
    ASSERT_TRUE( certificate.has_value() );
    const auto result = CertificateFallbackTestAccess::OnConsensusCertificate( *tm_,
                                                                               winner->GetHash(),
                                                                               certificate.value() );
    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( result.value(), ConsensusManager::Check::Approve );

    const auto tracked = CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, winner->GetHash() );
    ASSERT_TRUE( tracked.has_value() );
    EXPECT_EQ( tracked->status, TransactionManager::TransactionStatus::CONFIRMED );
}

/**
 * The key path is only a lookup hint: a decoded CRDT payload must still carry
 * the requested hash before it can become a certificate-first candidate.
 */
TEST_F( CertificateFallbackTest, ExactCrdtLookupIgnoresMismatchedPayloadHash )
{
    const auto requested  = MakeCompetingMintV2( account_->GetAddress(), 93 );
    const auto mismatched = MakeCompetingMintV2( account_->GetAddress(), 94 );
    ASSERT_NE( requested->GetHash(), mismatched->GetHash() );

    crdt::GlobalDB::Buffer serialized;
    serialized.put( mismatched->SerializeByteVector() );
    ASSERT_TRUE( db_->Put( { TransactionManager::GetTransactionPath( *requested ) }, serialized, {} ).has_value() );

    const auto recovered = CertificateFallbackTestAccess::FetchExactTransactionFromCRDT( *tm_, requested->GetHash() );
    ASSERT_TRUE( recovered.has_value() );
    EXPECT_FALSE( recovered.value().has_value() );
}

/**
 * CRDT storage is untrusted candidate data. A payload can claim the
 * certificate's data_hash while changing fields that are outside the Mint V2
 * slot identity, so exact hash-string comparison alone is insufficient.
 */
TEST_F( CertificateFallbackTest, ExactCrdtLookupRejectsForgedHashPayloadBeforeCertificateConsumption )
{
    const auto winner = MakeCompetingMintV2( account_->GetAddress(), 95 );
    ASSERT_TRUE( winner );
    const auto certificate = BuildSignedCertificate( winner );
    ASSERT_TRUE( certificate.has_value() );

    auto  forged       = winner->SerializeToEmbeddedTransaction();
    auto *extra_output = forged.mutable_mint_v2()->mutable_utxo_params()->add_outputs();
    extra_output->set_encrypted_amount( winner->GetAmount() + 1 );
    extra_output->set_dest_addr( "forged-mint-destination" );
    extra_output->set_token_id( kTestTokenId.bytes().data(), kTestTokenId.size() );
    // Keep the original data_hash so GetHash() still claims the certified hash.
    ASSERT_EQ( forged.mint_v2().dag_struct().data_hash(), winner->GetHash() );

    crdt::GlobalDB::Buffer serialized;
    serialized.put( forged.mint_v2().SerializeAsString() );
    ASSERT_TRUE( db_->Put( { TransactionManager::GetTransactionPath( *winner ) }, serialized, {} ).has_value() );

    const auto recovered = CertificateFallbackTestAccess::FetchExactTransactionFromCRDT( *tm_, winner->GetHash() );
    ASSERT_TRUE( recovered.has_value() );
    EXPECT_FALSE( recovered.value().has_value() );

    // The valid embedded winner is the constrained fallback; the forged CRDT
    // payload must never contribute its additional output.
    const auto result = CertificateFallbackTestAccess::OnConsensusCertificate( *tm_,
                                                                               winner->GetHash(),
                                                                               certificate.value() );
    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( result.value(), ConsensusManager::Check::Approve );
    const auto outputs = account_->GetUTXOManager().GetUTXOs( account_->GetAddress() );
    ASSERT_EQ( outputs.size(), 1u );
    EXPECT_EQ( outputs.front().GetAmount(), winner->GetAmount() );
}

/**
 * An accepted certificate for a Mint winner cannot promote an already tracked
 * contender that shares the winner's canonical slot.
 */
TEST_F( CertificateFallbackTest, CertificateFirstRejectsTrackedSameSlotLoser )
{
    const auto winner = MakeCompetingMintV2( account_->GetAddress(), 91 );
    const auto loser  = MakeCompetingMintV2( account_->GetAddress(), 92 );
    ASSERT_NE( winner->GetHash(), loser->GetHash() );
    ASSERT_EQ( winner->GetSlotID(), loser->GetSlotID() );

    ASSERT_TRUE( FetchAndProcess( loser ).has_value() );
    const auto before = CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, loser->GetHash() );
    ASSERT_TRUE( before.has_value() );
    EXPECT_EQ( before->status, TransactionManager::TransactionStatus::VERIFYING );

    const auto certificate = BuildSignedCertificate( winner );
    ASSERT_TRUE( certificate.has_value() );
    const auto result = CertificateFallbackTestAccess::OnConsensusCertificate( *tm_,
                                                                               loser->GetHash(),
                                                                               certificate.value() );
    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( result.value(), ConsensusManager::Check::Approve );

    const auto after = CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, loser->GetHash() );
    ASSERT_TRUE( after.has_value() );
    EXPECT_EQ( after->status, TransactionManager::TransactionStatus::VERIFYING );
}

/**
 * Missing and malformed authoritative slot records are not finality evidence.
 * They leave incoming transactions in VERIFYING rather than confirming them.
 */
TEST_F( CertificateFallbackTest, MissingOrMalformedMintSlotRecordFailsClosed )
{
    const auto missing_record   = MakeCompetingMintV2( account_->GetAddress(), 80 );
    const auto malformed_record = MakeCompetingMintV2( account_->GetAddress(), 81 );
    ASSERT_EQ( missing_record->GetSlotID(), malformed_record->GetSlotID() );

    ASSERT_TRUE( FetchAndProcess( missing_record ).has_value() );
    const auto missing_tracked = CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, missing_record->GetHash() );
    ASSERT_TRUE( missing_tracked.has_value() );
    EXPECT_EQ( missing_tracked->status, TransactionManager::TransactionStatus::VERIFYING );

    crdt::GlobalDB::Buffer malformed;
    malformed.put( "not-a-certificate" );
    ASSERT_TRUE( db_->Put( { "/cert/" + malformed_record->GetSlotID() }, malformed, {} ).has_value() );
    ASSERT_TRUE( FetchAndProcess( malformed_record ).has_value() );
    const auto malformed_tracked = CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_,
                                                                                      malformed_record->GetHash() );
    ASSERT_TRUE( malformed_tracked.has_value() );
    EXPECT_EQ( malformed_tracked->status, TransactionManager::TransactionStatus::VERIFYING );
}

/**
 * D-02/D-06/D-07/D-08: A pre-commit callback must not execute a certified Mint.
 * Once its exact canonical certificate is durable, the registered TransactionManager
 * handler applies UTXOs before attempting the bridge marker. A marker-only failure
 * leaves shared certificate work stalled for a duplicate-safe durable replay.
 */
TEST_F( CertificateFallbackTest, CertificateCallbackMarkerWriteFailureStallsThenRecoversExactlyOnce )
{
    const auto winner = MakeCompetingMintV2( account_->GetAddress(), 120 );
    ASSERT_TRUE( winner );
    const auto certificate = BuildSignedCertificate( winner );
    ASSERT_TRUE( certificate.has_value() );

    const auto manager = CertificateFallbackTestAccess::ConsensusManagerOf( *blockchain_ );
    ASSERT_TRUE( manager );
    const auto certificate_key = CertificateFallbackTestAccess::GetExpectedCertificateSlotKey( certificate.value() );
    ASSERT_EQ( certificate_key, std::string( "/cert/" ) + winner->GetSlotID() );

    std::string serialized;
    ASSERT_TRUE( certificate.value().SerializeToString( &serialized ) );
    crdt::GlobalDB::Buffer callback_value;
    callback_value.put( serialized );
    CertificateFallbackTestAccess::CertificateReceived(
        manager,
        crdt::CRDTCallbackManager::NewDataPair{ certificate_key, std::move( callback_value ) } );
    EXPECT_TRUE( CertificateFallbackTestAccess::HasCertificateWorkState( manager,
                                                                         certificate_key,
                                                                         crdt::CRDTWorkJournal::State::Stalled ) );

    // The callback is pre-commit only. Durable readback below is the sole path
    // allowed to dispatch the TransactionManager's registered certificate handler.
    PersistCertificateAtSlot( winner->GetSlotID(), certificate.value() );
    CertificateFallbackTestAccess::SetBridgeExecutedMarkerWriteFailure( *tm_, true );
    CertificateFallbackTestAccess::RecoverPendingCertificateWork( manager );

    const auto             marker_key = std::string( "/bridge/executed/source-chain:" ) + winner->dag_st.uncle_hash();
    crdt::GlobalDB::Buffer marker_key_buffer;
    marker_key_buffer.put( marker_key );
    EXPECT_TRUE( db_->GetDataStore()->get( marker_key_buffer ).has_error() );
    EXPECT_EQ( account_->GetUTXOManager().GetUTXOs( account_->GetAddress() ).size(), 1u );
    EXPECT_EQ( account_->GetUTXOManager().GetBalance(), winner->GetAmount() );
    const auto stalled_tracked = CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, winner->GetHash() );
    ASSERT_TRUE( stalled_tracked.has_value() );
    EXPECT_NE( stalled_tracked->status, TransactionManager::TransactionStatus::CONFIRMED );
    EXPECT_TRUE( CertificateFallbackTestAccess::HasCertificateWorkState( manager,
                                                                         certificate_key,
                                                                         crdt::CRDTWorkJournal::State::Stalled ) );

    CertificateFallbackTestAccess::SetBridgeExecutedMarkerWriteFailure( *tm_, false );
    CertificateFallbackTestAccess::RecoverPendingCertificateWork( manager );

    EXPECT_TRUE( db_->GetDataStore()->get( marker_key_buffer ).has_value() );
    const auto confirmed_tracked = CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, winner->GetHash() );
    ASSERT_TRUE( confirmed_tracked.has_value() );
    EXPECT_EQ( confirmed_tracked->status, TransactionManager::TransactionStatus::CONFIRMED );
    EXPECT_TRUE( CertificateFallbackTestAccess::HasNoCertificateWork( manager, certificate_key ) );

    // Duplicate certificate delivery must repeat only durable recovery, never Mint effects.
    crdt::GlobalDB::Buffer duplicate_callback_value;
    duplicate_callback_value.put( serialized );
    CertificateFallbackTestAccess::CertificateReceived(
        manager,
        crdt::CRDTCallbackManager::NewDataPair{ certificate_key, std::move( duplicate_callback_value ) } );
    CertificateFallbackTestAccess::RecoverPendingCertificateWork( manager );
    EXPECT_EQ( account_->GetUTXOManager().GetUTXOs( account_->GetAddress() ).size(), 1u );
    EXPECT_EQ( account_->GetUTXOManager().GetBalance(), winner->GetAmount() );
    EXPECT_TRUE( CertificateFallbackTestAccess::HasNoCertificateWork( manager, certificate_key ) );
}

/**
 * A failed UTXO snapshot must not leave an in-memory outpoint that turns the
 * next certificate replay into a false idempotent success. Reloading the
 * manager exercises the same durable view a restart would use.
 */
TEST_F( CertificateFallbackTest, CertificateFirstUtxoStoreFailureRetriesFromDurableState )
{
    const auto winner = MakeCompetingMintV2( account_->GetAddress(), 121 );
    ASSERT_TRUE( winner );
    const auto certificate = BuildSignedCertificate( winner );
    ASSERT_TRUE( certificate.has_value() );

    const auto             marker_key = std::string( "/bridge/executed/source-chain:" ) + winner->dag_st.uncle_hash();
    crdt::GlobalDB::Buffer marker_key_buffer;
    marker_key_buffer.put( marker_key );

    account_->GetUTXOManager().ReleaseStorage();
    const auto failed = CertificateFallbackTestAccess::OnConsensusCertificate( *tm_,
                                                                               winner->GetHash(),
                                                                               certificate.value() );
    EXPECT_TRUE( failed.has_error() );
    EXPECT_TRUE( db_->GetDataStore()->get( marker_key_buffer ).has_error() );
    EXPECT_TRUE( account_->GetUTXOManager().GetUTXOs( account_->GetAddress() ).empty() );

    // Reloading after the failed write must still observe no durable output.
    ASSERT_TRUE( account_->GetUTXOManager().LoadUTXOs( db_->GetDataStore() ).has_value() );
    EXPECT_TRUE( account_->GetUTXOManager().GetUTXOs( account_->GetAddress() ).empty() );

    const auto recovered = CertificateFallbackTestAccess::OnConsensusCertificate( *tm_,
                                                                                  winner->GetHash(),
                                                                                  certificate.value() );
    ASSERT_TRUE( recovered.has_value() );
    EXPECT_EQ( recovered.value(), ConsensusManager::Check::Approve );
    EXPECT_TRUE( db_->GetDataStore()->get( marker_key_buffer ).has_value() );
    EXPECT_EQ( account_->GetUTXOManager().GetUTXOs( account_->GetAddress() ).size(), 1u );

    const auto tracked = CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, winner->GetHash() );
    ASSERT_TRUE( tracked.has_value() );
    EXPECT_EQ( tracked->status, TransactionManager::TransactionStatus::CONFIRMED );
}

/**
 * A second ingress must not interpret an output inserted by a failing first
 * ingress as durable idempotent progress. The friend-only UTXO seam blocks the
 * first snapshot while it owns the registry lock, then fails that exact store.
 * The normal CRDT path is paused immediately before its state transition while
 * certificate ingress owns the in-flight UTXO snapshot.
 */
TEST_F( CertificateFallbackTest, ConcurrentCertificateIngressWaitsForDurableUtxoProgress )
{
    const auto winner = MakeCompetingMintV2( account_->GetAddress(), 122 );
    ASSERT_TRUE( winner );
    const auto certificate = BuildSignedCertificate( winner );
    ASSERT_TRUE( certificate.has_value() );

    const auto             marker_key = std::string( "/bridge/executed/source-chain:" ) + winner->dag_st.uncle_hash();
    crdt::GlobalDB::Buffer marker_key_buffer;
    marker_key_buffer.put( marker_key );

    auto                                                   &utxo_manager = account_->GetUTXOManager();
    std::mutex                                              barrier_mutex;
    std::condition_variable                                 barrier_cv;
    bool                                                    crdt_ready          = false;
    bool                                                    release_crdt        = false;
    bool                                                    first_store_entered = false;
    bool                                                    release_first_store = false;
    std::size_t                                             hook_calls          = 0;
    std::optional<outcome::result<ConsensusManager::Check>> first_result;
    std::optional<outcome::result<void>>                    crdt_result;

    PersistCertificateAtSlot( winner->GetSlotID(), certificate.value() );
    CertificateFallbackTestAccess::SetFailNextPutUTXOStore( utxo_manager, true );
    CertificateFallbackTestAccess::SetFetchAndProcessBeforeStateChangeHook(
        *tm_,
        [&]
        {
            std::unique_lock lock( barrier_mutex );
            crdt_ready = true;
            barrier_cv.notify_all();
            barrier_cv.wait( lock, [&] { return release_crdt; } );
        } );
    CertificateFallbackTestAccess::SetPutUTXOBeforeStoreHook(
        utxo_manager,
        [&]
        {
            std::unique_lock lock( barrier_mutex );
            if ( hook_calls++ != 0 )
            {
                return;
            }
            first_store_entered = true;
            barrier_cv.notify_all();
            barrier_cv.wait( lock, [&] { return release_first_store; } );
        } );

    std::thread crdt_ingress(
        [&]
        {
            base::Buffer serialized;
            serialized.put( winner->SerializeByteVector() );
            crdt_result = CertificateFallbackTestAccess::FetchAndProcessTransaction(
                *tm_,
                TransactionManager::GetTransactionPath( *winner ),
                std::move( serialized ) );
        } );

    {
        std::unique_lock lock( barrier_mutex );
        barrier_cv.wait( lock, [&] { return crdt_ready; } );
    }

    std::thread first_ingress(
        [&]
        {
            first_result = CertificateFallbackTestAccess::OnConsensusCertificate( *tm_,
                                                                                  winner->GetHash(),
                                                                                  certificate.value() );
        } );

    {
        std::unique_lock lock( barrier_mutex );
        barrier_cv.wait( lock, [&] { return first_store_entered; } );
    }

    {
        std::lock_guard lock( barrier_mutex );
        release_crdt = true;
    }
    barrier_cv.notify_all();

    // While the first insertion is still awaiting its durable snapshot, no
    // normal CRDT confirmation may create bridge completion evidence or a terminal state.
    EXPECT_TRUE( db_->GetDataStore()->get( marker_key_buffer ).has_error() );
    const auto in_flight = CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, winner->GetHash() );
    ASSERT_TRUE( in_flight.has_value() );
    EXPECT_NE( in_flight->status, TransactionManager::TransactionStatus::CONFIRMED );

    {
        std::lock_guard lock( barrier_mutex );
        release_first_store = true;
    }
    barrier_cv.notify_all();
    first_ingress.join();
    crdt_ingress.join();
    CertificateFallbackTestAccess::SetPutUTXOBeforeStoreHook( utxo_manager, {} );
    CertificateFallbackTestAccess::SetFetchAndProcessBeforeStateChangeHook( *tm_, {} );

    ASSERT_TRUE( first_result.has_value() );
    EXPECT_TRUE( first_result->has_error() );
    ASSERT_TRUE( crdt_result.has_value() );
    EXPECT_TRUE( crdt_result->has_value() );
    EXPECT_TRUE( db_->GetDataStore()->get( marker_key_buffer ).has_value() );
    EXPECT_EQ( utxo_manager.GetUTXOs( account_->GetAddress() ).size(), 1u );

    const auto confirmed = CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, winner->GetHash() );
    ASSERT_TRUE( confirmed.has_value() );
    EXPECT_EQ( confirmed->status, TransactionManager::TransactionStatus::CONFIRMED );
}
