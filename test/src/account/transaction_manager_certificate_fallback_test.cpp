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
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>

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

        static void CertificateReceived( const std::shared_ptr<ConsensusManager>          &manager,
                                         crdt::CRDTCallbackManager::NewDataPair             new_data )
        {
            manager->CertificateReceived( std::move( new_data ), std::string{} );
        }

        static void RecoverPendingCertificateWork( const std::shared_ptr<ConsensusManager> &manager )
        {
            manager->RecoverPendingCertificateWork();
        }

        static bool HasCertificateWorkState( const std::shared_ptr<ConsensusManager> &manager,
                                             const std::string                       &key,
                                             crdt::CRDTWorkJournal::State            state )
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
 * @brief Concrete CRDTFixture used as a plain object rather than a gtest fixture.
 * @details CRDTFixture builds the pubsub/GlobalDB stack in its constructor, so owning one
 *          as a suite-level object is what makes that cost per-suite instead of per-test.
 *          TestBody() only exists to satisfy ::testing::Test; it is never invoked.
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
        auto load_result = account_->GetUTXOManager().LoadUTXOs( crdt_->db_->GetDataStore() );
        ASSERT_TRUE( load_result.has_value() );

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

std::unique_ptr<SharedCrdtEnvironment> CertificateFallbackTest::crdt_;

/**
 * CONFLICT-01 / D-01/D-02/D-03: Certificate with a NonceSubject carrying a valid
 * embedded TransferTransaction, for a transaction this node has never seen.
 * OnConsensusCertificate takes the fallback path (GetTransactionByHash returns null),
 * deserializes the tx from the certificate, stores it, promotes it to CONFIRMED (so
 * tx_processed_m is populated for later HasConfirmedInputConflict checks), and approves.
 */
TEST_F( CertificateFallbackTest, HappyPath_FallbackDeserializesStoresAndConfirmsTx )
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

/**
 * Certificates whose subject yields no usable embedded transaction must be approved
 * without deserialization. Two distinct early returns are covered:
 *   1. an empty EmbeddedTransaction (TRANSACTION_NOT_SET) -- a pre-Phase-1 certificate;
 *   2. a subject that is not a NonceSubject at all, so DecodeNonceSubject fails.
 */
TEST_F( CertificateFallbackTest, EdgeCase_UndecodableSubjectsAreApprovedWithoutProcessing )
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
 * The hash-binding gate (`tx->GetHash() != tx_hash`) compares the deserialized embedded
 * transaction against the tx_hash *parameter*; the subject's own tx_hash field is never
 * consulted. Both ways of breaking that binding therefore reach the same guard, and
 * neither may process the embedded transaction.
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

    // 1. Subject and parameter agree on a hash that is not the embedded tx's hash.
    {
        const std::string mismatched_hash = "definitely-not-the-real-hash-value";
        const auto        subject         = MakeNonceSubject( account_->GetAddress(), 11, mismatched_hash, embedded );
        const auto        result          = CertificateFallbackTestAccess::OnConsensusCertificate(
            *tm_,
            mismatched_hash,
            BuildCertificate( subject, "proposal-mismatch-01" ) );
        ASSERT_TRUE( result.has_value() );
        EXPECT_EQ( result.value(), ConsensusManager::Check::Approve );
    }

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

    // First cert: fallback path, stores the tx.
    const auto subject_a = MakeNonceSubject( account_->GetAddress(), 30, tx_hash, embedded );
    const auto cert_a    = BuildCertificate( subject_a, "proposal-multi-a" );

    const auto result_a = CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, tx_hash, cert_a );
    ASSERT_TRUE( result_a.has_value() );
    EXPECT_EQ( result_a.value(), ConsensusManager::Check::Approve );
    EXPECT_NE( CertificateFallbackTestAccess::GetTransactionByHash( *tm_, tx_hash ), nullptr );

    // Second cert for the same tx: existing path.
    const auto subject_b = MakeNonceSubject( account_->GetAddress(), 30, tx_hash, embedded );
    const auto cert_b    = BuildCertificate( subject_b, "proposal-multi-b" );

    const auto result_b = CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, tx_hash, cert_b );
    ASSERT_TRUE( result_b.has_value() );
    EXPECT_EQ( result_b.value(), ConsensusManager::Check::Approve );

    // Still exactly one healthy, confirmed entry.
    EXPECT_NE( CertificateFallbackTestAccess::GetTransactionByHash( *tm_, tx_hash ), nullptr );
    const auto tracked = CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, tx_hash );
    ASSERT_TRUE( tracked.has_value() );
    EXPECT_EQ( tracked->status, TransactionManager::TransactionStatus::CONFIRMED );
}

TEST_F( CertificateFallbackTest, CertifiedWinnerImmediatelyFailsVerifyingTransactionsWithSameAddressAndNonce )
{
    const auto        &source = account_->GetAddress();
    constexpr uint64_t nonce  = 40;

    const auto loser_a_embedded        = MakeMinimalEmbeddedTransfer( *tm_, source, nonce, 1 );
    const auto loser_b_embedded        = MakeMinimalEmbeddedTransfer( *tm_, source, nonce, 2 );
    const auto winner_embedded         = MakeMinimalEmbeddedTransfer( *tm_, source, nonce, 3 );
    const auto other_embedded          = MakeMinimalEmbeddedTransfer( *tm_, source, nonce + 1, 4 );
    const auto other_address_embedded  = MakeMinimalEmbeddedTransfer( *tm_, "other-account", nonce, 5 );
    const auto already_failed_embedded = MakeMinimalEmbeddedTransfer( *tm_, source, nonce, 6 );

    const auto loser_a = CertificateFallbackTestAccess::DeSerializeEmbeddedTransaction( *tm_, loser_a_embedded )
                             .value();
    const auto loser_b = CertificateFallbackTestAccess::DeSerializeEmbeddedTransaction( *tm_, loser_b_embedded )
                             .value();
    const auto other   = CertificateFallbackTestAccess::DeSerializeEmbeddedTransaction( *tm_, other_embedded ).value();
    const auto other_address =
        CertificateFallbackTestAccess::DeSerializeEmbeddedTransaction( *tm_, other_address_embedded ).value();
    const auto already_failed =
        CertificateFallbackTestAccess::DeSerializeEmbeddedTransaction( *tm_, already_failed_embedded ).value();

    CertificateFallbackTestAccess::Track( *tm_, loser_a, TransactionManager::TransactionStatus::VERIFYING );
    CertificateFallbackTestAccess::Track( *tm_, loser_b, TransactionManager::TransactionStatus::VERIFYING );
    CertificateFallbackTestAccess::Track( *tm_, other, TransactionManager::TransactionStatus::VERIFYING );
    CertificateFallbackTestAccess::Track( *tm_, other_address, TransactionManager::TransactionStatus::VERIFYING );
    // A conflict that already failed must be left alone by the `continue` short-circuit,
    // without aborting the supersede loop for the conflicts that follow it.
    CertificateFallbackTestAccess::Track( *tm_, already_failed, TransactionManager::TransactionStatus::FAILED );

    const auto winner_hash = ComputeEmbeddedTxHash( *tm_, winner_embedded );
    const auto subject     = MakeNonceSubject( std::string( source ), nonce, winner_hash, winner_embedded );
    const auto result      = CertificateFallbackTestAccess::OnConsensusCertificate(
        *tm_,
        winner_hash,
        BuildCertificate( subject, "proposal-slot-winner-unknown" ) );

    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( result.value(), ConsensusManager::Check::Approve );
    EXPECT_EQ( CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, loser_a->GetHash() )->status,
               TransactionManager::TransactionStatus::FAILED );
    EXPECT_EQ( CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, loser_b->GetHash() )->status,
               TransactionManager::TransactionStatus::FAILED );
    EXPECT_EQ( CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, already_failed->GetHash() )->status,
               TransactionManager::TransactionStatus::FAILED );
    EXPECT_EQ( CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, winner_hash )->status,
               TransactionManager::TransactionStatus::CONFIRMED );
    EXPECT_EQ( CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, other->GetHash() )->status,
               TransactionManager::TransactionStatus::VERIFYING );
    EXPECT_EQ( CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, other_address->GetHash() )->status,
               TransactionManager::TransactionStatus::VERIFYING );
}

/**
 * The losing transaction must be superseded regardless of
 *   - which non-terminal state it happens to be in when the certificate lands, and
 *   - whether the winner is already tracked locally (and in which insertion order) or has
 *     to be reconstructed from the certificate.
 *
 * The tracking-order axis guards the removed GetTransactionByNonceAndAddress, which
 * returned only the first match and so depended on tx_processed_m iteration order.
 */
TEST_F( CertificateFallbackTest, ConflictIsSupersededAcrossLoserStatesAndTrackingOrders )
{
    enum class WinnerTracking : uint8_t
    {
        Untracked,   ///< Winner unknown locally -- reconstructed from the certificate.
        BeforeLoser, ///< Winner already tracked, inserted before the loser.
        AfterLoser   ///< Winner already tracked, inserted after the loser.
    };

    uint64_t next_nonce = 50;

    const auto run_case = [&]( TransactionManager::TransactionStatus loser_status, WinnerTracking winner_tracking )
    {
        const uint64_t    nonce  = next_nonce++;
        const std::string source = "conflict-matrix-" + std::to_string( nonce );

        const auto loser_embedded  = MakeMinimalEmbeddedTransfer( *tm_, source, nonce, 1 );
        const auto winner_embedded = MakeMinimalEmbeddedTransfer( *tm_, source, nonce, 2 );
        const auto loser  = CertificateFallbackTestAccess::DeSerializeEmbeddedTransaction( *tm_, loser_embedded )
                                .value();
        const auto winner = CertificateFallbackTestAccess::DeSerializeEmbeddedTransaction( *tm_, winner_embedded )
                                .value();

        const auto track_loser  = [&] { CertificateFallbackTestAccess::Track( *tm_, loser, loser_status ); };
        const auto track_winner = [&]
        { CertificateFallbackTestAccess::Track( *tm_, winner, TransactionManager::TransactionStatus::VERIFYING ); };

        switch ( winner_tracking )
        {
            case WinnerTracking::Untracked:
                track_loser();
                break;
            case WinnerTracking::BeforeLoser:
                track_winner();
                track_loser();
                break;
            case WinnerTracking::AfterLoser:
                track_loser();
                track_winner();
                break;
        }

        const auto subject = MakeNonceSubject( source, nonce, winner->GetHash(), winner_embedded );
        const auto result  = CertificateFallbackTestAccess::OnConsensusCertificate(
            *tm_,
            winner->GetHash(),
            BuildCertificate( subject, "proposal-matrix-" + std::to_string( nonce ) ) );

        const std::string context = "loser_status=" + std::to_string( static_cast<int>( loser_status ) ) +
                                    " winner_tracking=" + std::to_string( static_cast<int>( winner_tracking ) );

        ASSERT_TRUE( result.has_value() ) << context;
        EXPECT_EQ( result.value(), ConsensusManager::Check::Approve ) << context;
        EXPECT_EQ( CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, loser->GetHash() )->status,
                   TransactionManager::TransactionStatus::FAILED )
            << context;
        EXPECT_EQ( CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, winner->GetHash() )->status,
                   TransactionManager::TransactionStatus::CONFIRMED )
            << context;
    };

    for ( const auto loser_status : { TransactionManager::TransactionStatus::CREATED,
                                      TransactionManager::TransactionStatus::SENDING,
                                      TransactionManager::TransactionStatus::VERIFYING,
                                      TransactionManager::TransactionStatus::UNCONFIRMED } )
    {
        run_case( loser_status, WinnerTracking::Untracked );
        run_case( loser_status, WinnerTracking::BeforeLoser );
        run_case( loser_status, WinnerTracking::AfterLoser );
    }
}

TEST_F( CertificateFallbackTest, ConfirmedConflictStallsContradictoryCertificate )
{
    constexpr std::string_view source = "contradictory-finality-account";
    constexpr uint64_t         nonce  = 60;

    const auto existing_embedded = MakeMinimalEmbeddedTransfer( *tm_, source, nonce, 1 );
    const auto winner_embedded   = MakeMinimalEmbeddedTransfer( *tm_, source, nonce, 2 );
    const auto existing = CertificateFallbackTestAccess::DeSerializeEmbeddedTransaction( *tm_, existing_embedded )
                              .value();
    CertificateFallbackTestAccess::Track( *tm_, existing, TransactionManager::TransactionStatus::CONFIRMED );

    const auto winner_hash = ComputeEmbeddedTxHash( *tm_, winner_embedded );
    const auto subject     = MakeNonceSubject( std::string( source ), nonce, winner_hash, winner_embedded );
    const auto result      = CertificateFallbackTestAccess::OnConsensusCertificate(
        *tm_,
        winner_hash,
        BuildCertificate( subject, "proposal-contradictory-finality" ) );

    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( result.value(), ConsensusManager::Check::Stalled );
    EXPECT_EQ( CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, existing->GetHash() )->status,
               TransactionManager::TransactionStatus::CONFIRMED );
    EXPECT_FALSE( CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, winner_hash ).has_value() );
}

/**
 * The point of failing the loser early rather than waiting out the TTL is to give the
 * funds back. Asserting only TrackedTx::status would miss that: ChangeTransactionState
 * releases locally reserved inputs on a pre-confirmation failure, and that branch is
 * gated on the transaction being owned by this account.
 */
TEST_F( CertificateFallbackTest, FailingLocalLoserReleasesReservedInputs )
{
    // Give the account a spendable UTXO by confirming a local mint.
    auto mint = std::make_shared<MintTransaction>(
        MintTransaction::New( 1,
                              std::string( GeniusTransaction::GENIUS_CHAIN_ID ),
                              kTestTokenId,
                              MakeLocalDag( *account_, 0 ) ) );
    mint->MakeSignature( *account_ );
    ASSERT_TRUE(
        CertificateFallbackTestAccess::ChangeState( *tm_, mint, TransactionManager::TransactionStatus::CONFIRMED )
            .has_value() );
    ASSERT_EQ( account_->GetUTXOManager().GetBalance(), 1U );

    const auto mint_outpoint = base::Hash256::fromReadableString( mint->GetHash() );
    ASSERT_TRUE( mint_outpoint.has_value() );

    // Build a local transfer that reserves that input, exactly as an outgoing tx would.
    constexpr uint64_t nonce  = 90;
    auto               params = account_->GetUTXOManager().CreateTxParameter( 1, "0x00", kTestTokenId );
    ASSERT_TRUE( params.has_value() );
    const auto inputs            = params.value().first;
    auto [tx_inputs, tx_outputs] = std::move( params.value() );
    auto loser                   = std::make_shared<TransferTransaction>(
        TransferTransaction::New( std::move( tx_inputs ), std::move( tx_outputs ), MakeLocalDag( *account_, nonce ) ) );
    loser->MakeSignature( *account_ );
    account_->GetUTXOManager().ReserveUTXOs( inputs, loser->GetHash() );
    ASSERT_TRUE( account_->GetUTXOManager().IsOutPointReserved( mint_outpoint.value(), 0 ) );

    CertificateFallbackTestAccess::Track( *tm_, loser, TransactionManager::TransactionStatus::VERIFYING );

    // A different transaction wins the same address+nonce slot.
    const auto winner_embedded = MakeMinimalEmbeddedTransfer( *tm_, account_->GetAddress(), nonce, 7 );
    const auto winner_hash     = ComputeEmbeddedTxHash( *tm_, winner_embedded );
    const auto subject         = MakeNonceSubject( account_->GetAddress(), nonce, winner_hash, winner_embedded );
    const auto result          = CertificateFallbackTestAccess::OnConsensusCertificate(
        *tm_,
        winner_hash,
        BuildCertificate( subject, "proposal-local-loser-utxo" ) );

    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( result.value(), ConsensusManager::Check::Approve );
    EXPECT_EQ( CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, loser->GetHash() )->status,
               TransactionManager::TransactionStatus::FAILED );

    // The reservation is gone and the input is spendable again -- without waiting for the TTL.
    EXPECT_FALSE( account_->GetUTXOManager().IsOutPointReserved( mint_outpoint.value(), 0 ) );
    EXPECT_EQ( account_->GetUTXOManager().GetOutPointState( mint_outpoint.value(), 0 ),
               UTXOManager::UTXOState::UTXO_READY );
}

/**
 * Every other test calls OnConsensusCertificate directly, which would keep passing even
 * if TransactionManager::New stopped registering the handler. Drive the certificate
 * through the handler ConsensusManager would actually dispatch to instead.
 */
TEST_F( CertificateFallbackTest, RegisteredCertificateHandlerRoutesToConflictResolution )
{
    const auto handler = CertificateFallbackTestAccess::FindCertificateHandler( *blockchain_, NONCE_SUBJECT_TYPE );
    ASSERT_TRUE( handler ) << "TransactionManager::New did not register a certificate handler for "
                           << NONCE_SUBJECT_TYPE;

    const auto        &source = account_->GetAddress();
    constexpr uint64_t nonce  = 100;

    const auto loser_embedded  = MakeMinimalEmbeddedTransfer( *tm_, source, nonce, 1 );
    const auto winner_embedded = MakeMinimalEmbeddedTransfer( *tm_, source, nonce, 2 );
    const auto loser = CertificateFallbackTestAccess::DeSerializeEmbeddedTransaction( *tm_, loser_embedded ).value();
    CertificateFallbackTestAccess::Track( *tm_, loser, TransactionManager::TransactionStatus::VERIFYING );

    const auto winner_hash = ComputeEmbeddedTxHash( *tm_, winner_embedded );
    const auto subject     = MakeNonceSubject( std::string( source ), nonce, winner_hash, winner_embedded );

    // ConsensusManager keys the dispatch on the subject hash, not the tx hash. For nonce
    // subjects they coincide -- assert that, so a future divergence is caught here rather
    // than silently routing a certificate to the wrong transaction.
    const auto subject_hash = CertificateFallbackTestAccess::GetSubjectHash( subject );
    ASSERT_TRUE( subject_hash.has_value() );
    EXPECT_EQ( subject_hash.value(), winner_hash );

    const auto result = handler( subject_hash.value(), BuildCertificate( subject, "proposal-dispatched" ) );

    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( result.value(), ConsensusManager::Check::Approve );
    EXPECT_EQ( CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, loser->GetHash() )->status,
               TransactionManager::TransactionStatus::FAILED );
    EXPECT_EQ( CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, winner_hash )->status,
               TransactionManager::TransactionStatus::CONFIRMED );
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
    const auto result = CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, winner->GetHash(), certificate.value() );
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
    const auto requested = MakeCompetingMintV2( account_->GetAddress(), 93 );
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
    const auto result = CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, loser->GetHash(), certificate.value() );
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
    EXPECT_TRUE( CertificateFallbackTestAccess::HasCertificateWorkState(
        manager, certificate_key, crdt::CRDTWorkJournal::State::Stalled ) );

    // The callback is pre-commit only. Durable readback below is the sole path
    // allowed to dispatch the TransactionManager's registered certificate handler.
    PersistCertificateAtSlot( winner->GetSlotID(), certificate.value() );
    CertificateFallbackTestAccess::SetBridgeExecutedMarkerWriteFailure( *tm_, true );
    CertificateFallbackTestAccess::RecoverPendingCertificateWork( manager );

    const auto marker_key = std::string( "/bridge/executed/source-chain:" ) + winner->dag_st.uncle_hash();
    crdt::GlobalDB::Buffer marker_key_buffer;
    marker_key_buffer.put( marker_key );
    EXPECT_TRUE( db_->GetDataStore()->get( marker_key_buffer ).has_error() );
    EXPECT_EQ( account_->GetUTXOManager().GetUTXOs( account_->GetAddress() ).size(), 1u );
    EXPECT_EQ( account_->GetUTXOManager().GetBalance(), winner->GetAmount() );
    const auto stalled_tracked = CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, winner->GetHash() );
    ASSERT_TRUE( stalled_tracked.has_value() );
    EXPECT_NE( stalled_tracked->status, TransactionManager::TransactionStatus::CONFIRMED );
    EXPECT_TRUE( CertificateFallbackTestAccess::HasCertificateWorkState(
        manager, certificate_key, crdt::CRDTWorkJournal::State::Stalled ) );

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

    const auto marker_key = std::string( "/bridge/executed/source-chain:" ) + winner->dag_st.uncle_hash();
    crdt::GlobalDB::Buffer marker_key_buffer;
    marker_key_buffer.put( marker_key );

    account_->GetUTXOManager().ReleaseStorage();
    const auto failed = CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, winner->GetHash(), certificate.value() );
    EXPECT_TRUE( failed.has_error() );
    EXPECT_TRUE( db_->GetDataStore()->get( marker_key_buffer ).has_error() );
    EXPECT_TRUE( account_->GetUTXOManager().GetUTXOs( account_->GetAddress() ).empty() );

    // Reloading after the failed write must still observe no durable output.
    ASSERT_TRUE( account_->GetUTXOManager().LoadUTXOs( db_->GetDataStore() ).has_value() );
    EXPECT_TRUE( account_->GetUTXOManager().GetUTXOs( account_->GetAddress() ).empty() );

    const auto recovered =
        CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, winner->GetHash(), certificate.value() );
    ASSERT_TRUE( recovered.has_value() );
    EXPECT_EQ( recovered.value(), ConsensusManager::Check::Approve );
    EXPECT_TRUE( db_->GetDataStore()->get( marker_key_buffer ).has_value() );
    EXPECT_EQ( account_->GetUTXOManager().GetUTXOs( account_->GetAddress() ).size(), 1u );

    const auto tracked = CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, winner->GetHash() );
    ASSERT_TRUE( tracked.has_value() );
    EXPECT_EQ( tracked->status, TransactionManager::TransactionStatus::CONFIRMED );
}
