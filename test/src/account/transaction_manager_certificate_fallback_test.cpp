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
#include <chrono>
#include <string>

#include <boost/filesystem/operations.hpp>

#include "account/TransactionManager.hpp"
#include "account/TransferTransaction.hpp"
#include "account/GeniusAccount.hpp"
#include "blockchain/Blockchain.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"
#include "account/proto/SGTransaction.pb.h"
#include "crypto/hasher.hpp"
#include <gsl/span>
#include "testutil/storage/base_crdt_test.hpp"

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
            TransactionManager          &tm,
            const EmbeddedTransaction   &embedded )
        {
            return tm.DeSerializeEmbeddedTransaction( embedded );
        }

        static outcome::result<ConsensusManager::Check> OnConsensusCertificate(
            TransactionManager       &tm,
            const std::string        &tx_hash,
            const ConsensusCertificate &cert )
        {
            return tm.OnConsensusCertificate( tx_hash, cert );
        }

        static std::shared_ptr<GeniusTransaction> GetTransactionByHash(
            TransactionManager &tm,
            const std::string  &hash )
        {
            return tm.GetTransactionByHash( hash );
        }

        static std::optional<TransactionManager::TrackedTx> GetTrackedTxByHash(
            TransactionManager &tm,
            const std::string  &hash )
        {
            return tm.GetTrackedTxByHash( hash );
        }
    };
}  // namespace sgns

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
    ConsensusCertificate BuildCertificate( const ConsensusManager::Subject &subject,
                                           const std::string               &proposal_id )
    {
        ConsensusCertificate cert;
        cert.set_proposal_id( proposal_id );
        const auto now_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch() )
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
    EmbeddedTransaction MakeMinimalEmbeddedTransfer( TransactionManager &tm )
    {
        // Step 1: Create a bare TransferTx proto (no data_hash)
        SGTransaction::TransferTx bare_tx;
        bare_tx.mutable_dag_struct()->set_type( "transfer" );

        EmbeddedTransaction bare_embedded;
        *bare_embedded.mutable_transfer() = bare_tx;

        // Step 2: Deserialize to get a real TransferTransaction object
        auto deser_result =
            CertificateFallbackTestAccess::DeSerializeEmbeddedTransaction( tm, bare_embedded );
        assert( deser_result.has_value() && deser_result.value() != nullptr );
        auto tx_obj = deser_result.value();

        // Step 3: Compute hash using SerializeByteVector (matching CheckHash path)
        SGTransaction::DAGStruct dag_copy = tx_obj->dag_st;
        dag_copy.clear_signature();
        dag_copy.clear_data_hash();
        auto   serialized_bytes = tx_obj->SerializeByteVector( dag_copy );
        auto hash = sgns::crypto::blake2b_256(
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
    std::string ComputeEmbeddedTxHash( TransactionManager         &tm,
                                       const EmbeddedTransaction  &embedded )
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
        auto result = ConsensusManager::CreateNonceSubject(
            account_id, nonce, tx_hash, embedded, std::nullopt, std::nullopt );
        return result.value();
    }
}  // anonymous namespace

/**
 * @brief Lightweight test fixture using CRDTFixture for database/pubsub and
 *        creating a TransactionManager directly (no GeniusNode, no network sync).
 */
class CertificateFallbackTest : public test::CRDTFixture
{
public:
    CertificateFallbackTest() : CRDTFixture( "cert_fallback_test" )
    {
        // Create a GeniusAccount for the TransactionManager (random key, no crypto derivation)
        account_ = GeniusAccount::New( kTestTokenId, base_path / "account" );
        assert( account_ != nullptr );

        // Load the UTXOManager's DB so ParseTransaction can store UTXOs
        auto load_result = account_->GetUTXOManager().LoadUTXOs( db_->GetDataStore() );
        assert( load_result.has_value() );

        // Create a Blockchain with a no-op callback
        blockchain_ = Blockchain::New(
            db_, account_, pubs_, []( outcome::result<void> ) {} );
        assert( blockchain_ != nullptr );

        // Create a TransactionManager in non-full-node mode
        constexpr auto kTimestampTolerance = std::chrono::milliseconds( 300000 );
        constexpr auto kMutabilityWindow   = std::chrono::milliseconds( 600000 );

        tm_ = TransactionManager::New(
            db_, io_, account_,
            blockchain_,
            false,  // full_node
            0,      // subnet_id
            kTimestampTolerance,
            kMutabilityWindow );
        assert( tm_ != nullptr );
    }

    ~CertificateFallbackTest() override = default;

    std::shared_ptr<GeniusAccount>        account_;
    std::shared_ptr<Blockchain>           blockchain_;
    std::shared_ptr<TransactionManager>   tm_;
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
    const auto        embedded = MakeMinimalEmbeddedTransfer( *tm_ );
    const std::string tx_hash  = ComputeEmbeddedTxHash( *tm_, embedded );
    ASSERT_FALSE( tx_hash.empty() );

    const auto subject = MakeNonceSubject( account_->GetAddress(), 1, tx_hash, embedded );
    const auto cert    = BuildCertificate( subject, "proposal-happy-01" );

    const auto result =
        CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, tx_hash, cert );
    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( result.value(), ConsensusManager::Check::Approve );
}

/**
 * CONFLICT-01 / D-03: After certificate fallback processing, GetTransactionByHash
 * returns a non-null entry for the deserialized tx.
 */
TEST_F( CertificateFallbackTest, HappyPath_TxStoredAfterFallback )
{
    const auto        embedded = MakeMinimalEmbeddedTransfer( *tm_ );
    const std::string tx_hash  = ComputeEmbeddedTxHash( *tm_, embedded );
    ASSERT_FALSE( tx_hash.empty() );

    const auto subject = MakeNonceSubject( account_->GetAddress(), 2, tx_hash, embedded );
    const auto cert    = BuildCertificate( subject, "proposal-stored-01" );

    // Before: tx is not in the local store
    EXPECT_EQ( CertificateFallbackTestAccess::GetTransactionByHash( *tm_, tx_hash ), nullptr );

    const auto result =
        CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, tx_hash, cert );
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
    const auto        embedded = MakeMinimalEmbeddedTransfer( *tm_ );
    const std::string tx_hash  = ComputeEmbeddedTxHash( *tm_, embedded );
    ASSERT_FALSE( tx_hash.empty() );

    const auto subject = MakeNonceSubject( account_->GetAddress(), 3, tx_hash, embedded );
    const auto cert    = BuildCertificate( subject, "proposal-confirmed-01" );

    const auto result =
        CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, tx_hash, cert );
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
    const auto subject = ConsensusManager::CreateNonceSubject(
                             account_->GetAddress(), 10, "fake-hash-empty",
                             EmbeddedTransaction{}, std::nullopt, std::nullopt )
                             .value();
    const auto cert = BuildCertificate( subject, "proposal-empty-01" );

    const auto result =
        CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, "fake-hash-empty", cert );
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
    const auto                 subject = ConsensusManager::CreateGenericSubject(
                                              account_->GetAddress(), "gnus.bridge_event.v1", payload )
                                              .value();
    const auto cert = BuildCertificate( subject, "proposal-generic-01" );

    const auto result =
        CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, "fake-hash-generic", cert );
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
    const auto        embedded        = MakeMinimalEmbeddedTransfer( *tm_ );
    const std::string real_hash       = ComputeEmbeddedTxHash( *tm_, embedded );
    const std::string mismatched_hash = "definitely-not-the-real-hash-value";

    const auto subject = MakeNonceSubject( account_->GetAddress(), 11, mismatched_hash, embedded );
    const auto cert    = BuildCertificate( subject, "proposal-mismatch-01" );

    const auto result =
        CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, mismatched_hash, cert );
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
    const auto        embedded  = MakeMinimalEmbeddedTransfer( *tm_ );
    const std::string real_hash = ComputeEmbeddedTxHash( *tm_, embedded );
    ASSERT_FALSE( real_hash.empty() );

    const auto subject = MakeNonceSubject( account_->GetAddress(), 12, real_hash, embedded );
    const auto cert    = BuildCertificate( subject, "proposal-param-diff-01" );

    const std::string wrong_param_hash = "some-other-hash-not-in-store";
    const auto        result =
        CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, wrong_param_hash, cert );
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
    const auto        embedded = MakeMinimalEmbeddedTransfer( *tm_ );
    const std::string tx_hash  = ComputeEmbeddedTxHash( *tm_, embedded );
    ASSERT_FALSE( tx_hash.empty() );

    // First certificate: enters fallback path, stores the tx
    const auto subject1 = MakeNonceSubject( account_->GetAddress(), 20, tx_hash, embedded );
    const auto cert1    = BuildCertificate( subject1, "proposal-regression-first" );

    const auto result1 =
        CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, tx_hash, cert1 );
    ASSERT_TRUE( result1.has_value() );
    EXPECT_EQ( result1.value(), ConsensusManager::Check::Approve );

    // Verify: tx is now in the store
    EXPECT_NE( CertificateFallbackTestAccess::GetTransactionByHash( *tm_, tx_hash ), nullptr );

    // Second certificate for the same tx: existing path (GetTransactionByHash returns non-null)
    const auto subject2 = MakeNonceSubject( account_->GetAddress(), 20, tx_hash, embedded );
    const auto cert2    = BuildCertificate( subject2, "proposal-regression-second" );

    const auto result2 =
        CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, tx_hash, cert2 );
    ASSERT_TRUE( result2.has_value() );
    EXPECT_EQ( result2.value(), ConsensusManager::Check::Approve );
}

/**
 * Multiple certificates for the same tx: idempotent behavior.
 * The tx is stored once and remains in the store after repeated certs.
 */
TEST_F( CertificateFallbackTest, MultipleCerts_SameTx_Idempotent )
{
    const auto        embedded = MakeMinimalEmbeddedTransfer( *tm_ );
    const std::string tx_hash  = ComputeEmbeddedTxHash( *tm_, embedded );
    ASSERT_FALSE( tx_hash.empty() );

    // First cert: stores the tx
    const auto subject_a = MakeNonceSubject( account_->GetAddress(), 30, tx_hash, embedded );
    const auto cert_a    = BuildCertificate( subject_a, "proposal-multi-a" );

    const auto result_a =
        CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, tx_hash, cert_a );
    ASSERT_TRUE( result_a.has_value() );
    EXPECT_EQ( result_a.value(), ConsensusManager::Check::Approve );
    EXPECT_NE( CertificateFallbackTestAccess::GetTransactionByHash( *tm_, tx_hash ), nullptr );

    // Second cert with same tx (idempotent -- already stored from first cert)
    const auto subject_b = MakeNonceSubject( account_->GetAddress(), 30, tx_hash, embedded );
    const auto cert_b    = BuildCertificate( subject_b, "proposal-multi-b" );

    const auto result_b =
        CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, tx_hash, cert_b );
    ASSERT_TRUE( result_b.has_value() );
    EXPECT_EQ( result_b.value(), ConsensusManager::Check::Approve );

    // Tx still in store (not duplicated or corrupted by second cert)
    EXPECT_NE( CertificateFallbackTestAccess::GetTransactionByHash( *tm_, tx_hash ), nullptr );
}
