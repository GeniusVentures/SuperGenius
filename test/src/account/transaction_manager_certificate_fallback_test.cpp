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
#include "account/MintTransaction.hpp"
#include "account/GeniusAccount.hpp"
#include "blockchain/Blockchain.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"
#include "account/proto/SGTransaction.pb.h"
#include "crypto/hasher.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
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
            TransactionManager        &tm,
            const EmbeddedTransaction &embedded )
        {
            return tm.DeSerializeEmbeddedTransaction( embedded );
        }

        static outcome::result<ConsensusManager::ApplicationDisposition> OnConsensusCertificate(
            TransactionManager         &tm,
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

        static void Track( TransactionManager                       &tm,
                           const std::shared_ptr<GeniusTransaction> &tx,
                           TransactionManager::TransactionStatus     status )
        {
            std::unique_lock lock( tm.tx_mutex_m );
            tm.tx_processed_m[TransactionManager::GetTransactionPath( *tx )] = TransactionManager::TrackedTx{
                tx,
                status,
                tx->GetNonce() };
        }

        static outcome::result<void> ChangeState( TransactionManager                       &tm,
                                                  const std::shared_ptr<GeniusTransaction> &tx,
                                                  TransactionManager::TransactionStatus     status )
        {
            return tm.ChangeTransactionState( tx, status );
        }

        /**
         * @brief Looks up the certificate handler ConsensusManager would dispatch to for
         *        @p subject_type, i.e. the one TransactionManager::New registered.
         * @return The registered handler, or nullptr when nothing is registered.
         */
        static ConsensusManager::CertificateSubjectHandler FindCertificateHandler( Blockchain      &blockchain,
                                                                                   std::string_view subject_type )
        {
            const auto &manager = blockchain.consensus_manager_;
            if ( !manager )
            {
                return nullptr;
            }
            auto type_hash = ConsensusManager::ComputeSubjectTypeHash( subject_type );
            if ( type_hash.has_error() )
            {
                return nullptr;
            }
            std::shared_lock lock( manager->certificate_handlers_mutex_ );
            auto             it = manager->certificate_subject_handlers_.find( type_hash.value() );
            if ( it == manager->certificate_subject_handlers_.end() )
            {
                return nullptr;
            }
            return it->second;
        }

        static outcome::result<std::string> GetSubjectHash( const ConsensusManager::Subject &subject )
        {
            return ConsensusManager::GetSubjectHash( subject );
        }
    };
}

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
                                                     std::string_view    source    = {},
                                                     uint64_t            nonce     = 0,
                                                     uint64_t            timestamp = 0 )
    {
        // Step 1: Create a bare TransferTx proto (no data_hash)
        SGTransaction::TransferTx bare_tx;
        bare_tx.mutable_dag_struct()->set_type( "transfer" );
        bare_tx.mutable_dag_struct()->set_source_addr( source.data(), source.size() );
        bare_tx.mutable_dag_struct()->set_nonce( nonce );
        bare_tx.mutable_dag_struct()->set_timestamp( timestamp );

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

    /**
     * @brief Builds a DAGStruct owned by @p account, so the local-account branches of
     *        ChangeTransactionState (UTXO release, nonce bookkeeping) are exercised.
     */
    SGTransaction::DAGStruct MakeLocalDag( const GeniusAccount &account, uint64_t nonce )
    {
        SGTransaction::DAGStruct dag;
        dag.set_nonce( nonce );
        dag.set_source_addr( account.GetAddress() );
        dag.set_timestamp( std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch() )
                               .count() );
        return dag;
    }
}

/**
 * @brief Concrete CRDTFixture used as a plain object rather than a gtest fixture.
 * @details CRDTFixture builds the pubsub/GlobalDB stack in its constructor, so owning one
 *          as a suite-level object is what makes that cost per-suite instead of per-test.
 *          TestBody() only exists to satisfy ::testing::Test; it is never invoked.
 */
class SharedCrdtEnvironment : public test::CRDTFixture
{
public:
    SharedCrdtEnvironment() : CRDTFixture( "cert_fallback_test" )
    {
    }

    void TestBody() override
    {
    }
};

/**
 * @brief Lightweight test fixture creating a TransactionManager directly (no GeniusNode,
 *        no network sync).
 *
 * The GossipPubSub/GlobalDB stack is built once per suite rather than per test. Only the account,
 * Blockchain and TransactionManager -- the cheap, stateful parts -- are rebuilt per test.
 *
 * Because the GlobalDB is now shared, each test gets a freshly generated account, and so
 * a distinct address. Address-keyed state (UTXOs, confirmed nonces) written by one test
 * is therefore invisible to the next. Do not rely on the account address being stable
 * across tests.
 */
class CertificateFallbackTest : public ::testing::Test
{
public:
    static void SetUpTestSuite()
    {
        test::CRDTFixture::SetUpTestSuite(); // logging system only
        crdt_ = std::make_unique<SharedCrdtEnvironment>();
    }

    static void TearDownTestSuite()
    {
        crdt_.reset();
        test::CRDTFixture::TearDownTestSuite();
    }

    void SetUp() override
    {
        ASSERT_NE( crdt_, nullptr );

        GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                                                { return std::make_shared<MemorySecureStorage>( identifier ); } );

        // MemorySecureStorage keys its (process-wide static) store by identifier, so a
        // unique path per test is what forces a fresh keypair and a fresh address.
        static std::atomic<uint64_t> account_counter{ 0 };
        const auto                   account_path =
            fs::path( crdt_->getPathString() ) /
            ( "account-" + std::to_string( account_counter.fetch_add( 1, std::memory_order_relaxed ) ) );

        account_ = GeniusAccount::New( kTestTokenId, account_path );
        ASSERT_NE( account_, nullptr );

        // Load the UTXOManager's DB so ParseTransaction can store UTXOs
        auto load_result = account_->GetUTXOManager().LoadUTXOs( crdt_->db_->GetDataStore() );
        ASSERT_TRUE( load_result.has_value() );

        // Create a Blockchain with a no-op callback
        blockchain_ = Blockchain::New( crdt_->db_, account_, crdt_->pubs_, []( outcome::result<void> ) {} );
        ASSERT_NE( blockchain_, nullptr );

        // Create a TransactionManager in non-full-node mode
        constexpr auto kTimestampTolerance = std::chrono::milliseconds( 300000 );
        constexpr auto kMutabilityWindow   = std::chrono::milliseconds( 600000 );

        tm_ = TransactionManager::New( crdt_->db_,
                                       crdt_->io_,
                                       account_,
                                       blockchain_,
                                       false, // full_node
                                       0,     // subnet_id
                                       kTimestampTolerance,
                                       kMutabilityWindow );
        ASSERT_NE( tm_, nullptr );
    }

    void TearDown() override
    {
        // The shared GlobalDB outlives this test, so drop this test's element filters and
        // callbacks before the next one registers its own.
        if ( tm_ )
        {
            tm_->Stop();
            tm_.reset();
        }
        if ( blockchain_ )
        {
            ( void ) blockchain_->Stop();
            blockchain_.reset();
        }
        account_.reset();
        GeniusAccount::SetSecureStorageFactory( nullptr );
    }

    ~CertificateFallbackTest() override = default;

    static std::unique_ptr<SharedCrdtEnvironment> crdt_;

    std::shared_ptr<GeniusAccount>      account_;
    std::shared_ptr<Blockchain>         blockchain_;
    std::shared_ptr<TransactionManager> tm_;
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
    const auto        embedded = MakeMinimalEmbeddedTransfer( *tm_ );
    const std::string tx_hash  = ComputeEmbeddedTxHash( *tm_, embedded );
    ASSERT_FALSE( tx_hash.empty() );

    const auto subject = MakeNonceSubject( account_->GetAddress(), 1, tx_hash, embedded );
    const auto cert    = BuildCertificate( subject, "proposal-happy-01" );

    // Before: the tx is unknown locally, so the certificate must take the fallback path.
    EXPECT_EQ( CertificateFallbackTestAccess::GetTransactionByHash( *tm_, tx_hash ), nullptr );

    const auto result = CertificateFallbackTestAccess::OnConsensusCertificate( *tm_, tx_hash, cert );
    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( result.value(), ConsensusManager::ApplicationDisposition::Applied );

    // After: the tx was reconstructed into the local store and confirmed.
    EXPECT_NE( CertificateFallbackTestAccess::GetTransactionByHash( *tm_, tx_hash ), nullptr );
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
    // 1. NonceSubject with no embedded transaction.
    {
        const auto subject = ConsensusManager::CreateNonceSubject( account_->GetAddress(),
                                                                   10,
                                                                   "fake-hash-empty",
                                                                   EmbeddedTransaction{},
                                                                   std::nullopt,
                                                                   std::nullopt )
                                 .value();
        const auto result = CertificateFallbackTestAccess::OnConsensusCertificate(
            *tm_,
            "fake-hash-empty",
            BuildCertificate( subject, "proposal-empty-01" ) );
        ASSERT_TRUE( result.has_value() );
        EXPECT_EQ( result.value(), ConsensusManager::ApplicationDisposition::Applied );
    }

    // 2. Subject of a different type entirely -- DecodeNonceSubject fails.
    {
        const std::vector<uint8_t> payload = { 0x01, 0x02, 0x03 };
        const auto                 subject =
            ConsensusManager::CreateGenericSubject( account_->GetAddress(), "gnus.bridge_event.v1", payload ).value();
        const auto result = CertificateFallbackTestAccess::OnConsensusCertificate(
            *tm_,
            "fake-hash-generic",
            BuildCertificate( subject, "proposal-generic-01" ) );
        ASSERT_TRUE( result.has_value() );
        EXPECT_EQ( result.value(), ConsensusManager::ApplicationDisposition::Applied );
    }
}

/**
 * The hash-binding gate (`tx->GetHash() != tx_hash`) compares the deserialized embedded
 * transaction against the tx_hash *parameter*; the subject's own tx_hash field is never
 * consulted. Both ways of breaking that binding therefore reach the same guard, and
 * neither may process the embedded transaction.
 */
TEST_F( CertificateFallbackTest, EdgeCase_HashBindingFailureApprovesWithoutProcessing )
{
    const auto        embedded  = MakeMinimalEmbeddedTransfer( *tm_ );
    const std::string real_hash = ComputeEmbeddedTxHash( *tm_, embedded );
    ASSERT_FALSE( real_hash.empty() );

    // 1. Subject and parameter agree on a hash that is not the embedded tx's hash.
    {
        const std::string mismatched_hash = "definitely-not-the-real-hash-value";
        const auto        subject = MakeNonceSubject( account_->GetAddress(), 11, mismatched_hash, embedded );
        const auto        result  = CertificateFallbackTestAccess::OnConsensusCertificate(
            *tm_,
            mismatched_hash,
            BuildCertificate( subject, "proposal-mismatch-01" ) );
        ASSERT_TRUE( result.has_value() );
        EXPECT_EQ( result.value(), ConsensusManager::ApplicationDisposition::Applied );
    }

    // 2. Subject carries the real hash but the parameter does not.
    {
        const std::string wrong_param_hash = "some-other-hash-not-in-store";
        const auto        subject          = MakeNonceSubject( account_->GetAddress(), 12, real_hash, embedded );
        const auto        result           = CertificateFallbackTestAccess::OnConsensusCertificate(
            *tm_,
            wrong_param_hash,
            BuildCertificate( subject, "proposal-param-diff-01" ) );
        ASSERT_TRUE( result.has_value() );
        EXPECT_EQ( result.value(), ConsensusManager::ApplicationDisposition::Applied );
    }

    // Neither attempt stored the embedded transaction.
    EXPECT_EQ( CertificateFallbackTestAccess::GetTransactionByHash( *tm_, real_hash ), nullptr );
}

/**
 * Repeated certificates for the same transaction are idempotent. The first takes the
 * fallback path and stores the tx; every later one takes the existing-tx path
 * (GetTransactionByHash returns non-null) and must still approve without duplicating or
 * corrupting the tracked entry.
 */
TEST_F( CertificateFallbackTest, MultipleCerts_SameTx_Idempotent )
{
    const auto        embedded = MakeMinimalEmbeddedTransfer( *tm_ );
    const std::string tx_hash  = ComputeEmbeddedTxHash( *tm_, embedded );
    ASSERT_FALSE( tx_hash.empty() );

    // First cert: fallback path, stores the tx.
    const auto subject_a = MakeNonceSubject( account_->GetAddress(), 30, tx_hash, embedded );
    const auto result_a  = CertificateFallbackTestAccess::OnConsensusCertificate(
        *tm_,
        tx_hash,
        BuildCertificate( subject_a, "proposal-multi-a" ) );
    ASSERT_TRUE( result_a.has_value() );
    EXPECT_EQ( result_a.value(), ConsensusManager::ApplicationDisposition::Applied );
    EXPECT_NE( CertificateFallbackTestAccess::GetTransactionByHash( *tm_, tx_hash ), nullptr );

    // Second cert for the same tx: existing path.
    const auto subject_b = MakeNonceSubject( account_->GetAddress(), 30, tx_hash, embedded );
    const auto result_b  = CertificateFallbackTestAccess::OnConsensusCertificate(
        *tm_,
        tx_hash,
        BuildCertificate( subject_b, "proposal-multi-b" ) );
    ASSERT_TRUE( result_b.has_value() );
    EXPECT_EQ( result_b.value(), ConsensusManager::ApplicationDisposition::Applied );

    // Still exactly one healthy, confirmed entry.
    EXPECT_NE( CertificateFallbackTestAccess::GetTransactionByHash( *tm_, tx_hash ), nullptr );
    const auto tracked = CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, tx_hash );
    ASSERT_TRUE( tracked.has_value() );
    EXPECT_EQ( tracked->status, TransactionManager::TransactionStatus::CONFIRMED );
}

TEST_F( CertificateFallbackTest, CertifiedWinnerImmediatelyFailsVerifyingTransactionsWithSameAddressAndNonce )
{
    const auto &source = account_->GetAddress();
    constexpr uint64_t nonce = 40;

    const auto loser_a_embedded       = MakeMinimalEmbeddedTransfer( *tm_, source, nonce, 1 );
    const auto loser_b_embedded       = MakeMinimalEmbeddedTransfer( *tm_, source, nonce, 2 );
    const auto winner_embedded        = MakeMinimalEmbeddedTransfer( *tm_, source, nonce, 3 );
    const auto other_embedded         = MakeMinimalEmbeddedTransfer( *tm_, source, nonce + 1, 4 );
    const auto other_address_embedded = MakeMinimalEmbeddedTransfer( *tm_, "other-account", nonce, 5 );
    const auto already_failed_embedded = MakeMinimalEmbeddedTransfer( *tm_, source, nonce, 6 );

    const auto loser_a = CertificateFallbackTestAccess::DeSerializeEmbeddedTransaction( *tm_, loser_a_embedded )
                             .value();
    const auto loser_b = CertificateFallbackTestAccess::DeSerializeEmbeddedTransaction( *tm_, loser_b_embedded )
                             .value();
    const auto other = CertificateFallbackTestAccess::DeSerializeEmbeddedTransaction( *tm_, other_embedded ).value();
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
    EXPECT_EQ( result.value(), ConsensusManager::ApplicationDisposition::Applied );
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
        const auto loser = CertificateFallbackTestAccess::DeSerializeEmbeddedTransaction( *tm_, loser_embedded )
                               .value();
        const auto winner = CertificateFallbackTestAccess::DeSerializeEmbeddedTransaction( *tm_, winner_embedded )
                                .value();

        const auto track_loser = [&]
        { CertificateFallbackTestAccess::Track( *tm_, loser, loser_status ); };
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
        EXPECT_EQ( result.value(), ConsensusManager::ApplicationDisposition::Applied ) << context;
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
    EXPECT_EQ( result.value(), ConsensusManager::ApplicationDisposition::Irreconcilable );
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
    ASSERT_TRUE( CertificateFallbackTestAccess::ChangeState( *tm_,
                                                             mint,
                                                             TransactionManager::TransactionStatus::CONFIRMED )
                     .has_value() );
    ASSERT_EQ( account_->GetUTXOManager().GetBalance(), 1U );

    const auto mint_outpoint = base::Hash256::fromReadableString( mint->GetHash() );
    ASSERT_TRUE( mint_outpoint.has_value() );

    // Build a local transfer that reserves that input, exactly as an outgoing tx would.
    constexpr uint64_t nonce  = 90;
    auto               params = account_->GetUTXOManager().CreateTxParameter( 1, "0x00", kTestTokenId );
    ASSERT_TRUE( params.has_value() );
    const auto inputs = params.value().first;
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
    const auto subject = MakeNonceSubject( account_->GetAddress(), nonce, winner_hash, winner_embedded );
    const auto result  = CertificateFallbackTestAccess::OnConsensusCertificate(
        *tm_,
        winner_hash,
        BuildCertificate( subject, "proposal-local-loser-utxo" ) );

    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( result.value(), ConsensusManager::ApplicationDisposition::Applied );
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
