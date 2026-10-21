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

#include "transaction/TransactionManager.hpp"
#include "transaction/TransactionConsensusHandler.hpp"
#include "account/TransferTransaction.hpp"
#include "account/MintTransactionV2.hpp"
#include "account/RegistrationTransaction.hpp"
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
            return tm.consensus_m_->OnConsensusCertificate( tx_hash, cert );
        }

        static std::shared_ptr<GeniusTransaction> GetTransactionByHash( TransactionManager &tm,
                                                                        const std::string  &hash )
        {
            return tm.GetTransactionByHash( hash );
        }

        static outcome::result<bool> CheckTransactionValidity( TransactionManager       &tm,
                                                              const std::set<uint64_t> &nonces )
        {
            return tm.CheckTransactionValidity( nonces );
        }

        static outcome::result<void> ParseTransaction( TransactionManager         &tm,
                                                       const std::shared_ptr<GeniusTransaction> &tx )
        {
            return tm.ParseTransaction( *tx );
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

        static std::string GetOutgoingPreviousHash( const TransactionManager &tm, uint64_t nonce )
        {
            return tm.GetOutgoingPreviousHash( nonce );
        }

        static std::string GetRegisteredOutgoingPreviousHash( const TransactionManager &tm, uint64_t nonce )
        {
            return tm.GetRegisteredOutgoingPreviousHash( nonce );
        }

        static std::string QueryOutgoingPreviousHashFromCRDT( const TransactionManager &tm, uint64_t nonce )
        {
            return tm.QueryOutgoingPreviousHashFromCRDT( nonce );
        }

        static outcome::result<void> StoreTransactionCID( TransactionManager &tm,
                                                          const std::string  &key,
                                                          const std::string  &cid )
        {
            return tm.StoreTransactionCID( key, cid );
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

        /// Drives the real proposal-ingress validation path (CheckProposal ->
        /// subject handlers -> HandleNonceConsensusSubject) synchronously.
        static void HandleProposal( const std::shared_ptr<ConsensusManager> &manager,
                                    const ConsensusManager::Proposal        &proposal )
        {
            manager->HandleProposal( proposal );
        }

        /// Parks the manager's round timer (the periodic dispatcher that
        /// replays durable certificate work) for tests that drive certificate
        /// ingress explicitly and must not race timer-thread dispatch. The
        /// handshake variant is required: stretching the round duration alone
        /// leaves one in-flight tick that can still dispatch work made durable
        /// immediately afterwards (OSX Debug CI, 2026-09-28/30).
        static void ParkRoundTimer( const std::shared_ptr<ConsensusManager> &manager )
        {
            manager->ParkRoundTimerForTest();
        }

        static bool HasCertificateWorkState( const std::shared_ptr<ConsensusManager> &manager,
                                             const std::string                       &key,
                                             crdt::CRDTWorkJournal::State             state )
        {
            const auto entry = manager->certificate_work_journal_->GetEntry( key );
            return entry.has_value() && entry->state == state;
        }

        /// True when the entry is Stalled with a live (not-yet-expired) backoff
        /// lease — i.e., the handler-failure backoff has engaged.
        static bool HasLiveBackoffLease( const std::shared_ptr<ConsensusManager> &manager, const std::string &key )
        {
            const auto entry = manager->certificate_work_journal_->GetEntry( key );
            if ( !entry.has_value() || entry->state != crdt::CRDTWorkJournal::State::Stalled )
            {
                return false;
            }
            const auto now_ms = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch() )
                    .count() );
            return entry->lease_until_ms != 0 && entry->lease_until_ms > now_ms;
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

    /// @brief The fixture GlobalDB's seeded listen+broadcast topic
    ///        (base_crdt_test.cpp). Real-topic Puts are load-bearing for the
    ///        erase lifecycle: an empty-topics Put writes no head and no seal,
    ///        and EraseTransactionEntry's resolved guard would then reject the
    ///        erase (Phase-16 empty-topics lesson).
    constexpr char kSeedTopic[] = "CRDT.Datastore.TEST.Channel";

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
                                       NodeType::Light, // node_type
                                       0,               // subnet_id
                                       kTimestampTolerance,
                                       kMutabilityWindow );
        assert( tm_ != nullptr );
    }

    ~CertificateFallbackTest() override
    {
        /*
         * Teardown invariant (asio), mirroring Peer::Stop in
         * multi_node_finality_fault_test.cpp: the ConsensusManager round-timer
         * thread created by Blockchain::New reprocesses pending certificate work
         * every >=500ms through this fixture's TransactionManager. A dispatch
         * that is in flight while the fixture is destroyed keeps the
         * TransactionManager (and, through its members, the Blockchain,
         * ValidatorRegistry and GlobalDB) alive past this destructor, so the
         * base ~CRDTFixture would then run pubs_->Stop() and free the libp2p
         * host/io_context underneath the still-running timer — the delayed
         * ~TransactionManager finally fires on the timer thread after gtest has
         * finished and segfaults (epoll reactor deref after free; Linux CI
         * crash in transaction_manager_certificate_fallback_test). Stop the
         * blockchain first: Blockchain::Stop -> ConsensusManager::Close joins
         * the round timer, releasing any in-flight strong reference while
         * db_/pubs_ are still intact. Only then release the db-backed handles;
         * the base destructor's db_.reset() -> pubs_->Stop() stays the FINAL
         * host release.
         */
        if ( blockchain_ )
        {
            (void) blockchain_->Stop();
        }
        tm_.reset();
        blockchain_.reset();
        account_.reset();
    }

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
                              ->StoreGenesisRegistry( { account_->GetAddress() },
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

    /**
     * @brief Seeds one frontier transaction into the exact state the Phase-16
     *        erase lifecycle requires (17-03 / GATE-04 SC-3).
     *
     * The value goes through a REAL-topic CRDT Put (never the empty-topic
     * PersistTransaction shape) so the delta CID, its head entry, and its
     * seal marker exist; a follow-up seed on the same topic retires the
     * frontier's CID from headship so EraseEntry's head guard accepts; the
     * raw tx->CID map entry is seeded with the exact StoreTransactionCID
     * key/value shapes so ResolveTransactionPathAndCID finds the entry's own
     * network path. The certificate at the frontier's slot is deliberately
     * NOT seeded here -- each case decides whether the cert authority exists.
     */
    void SeedErasableFrontierEntry( const std::shared_ptr<GeniusTransaction> &frontier )
    {
        const std::string tx_path = TransactionManager::GetTransactionPath( *frontier );

        crdt::GlobalDB::Buffer serialized;
        serialized.put( frontier->SerializeByteVector() );
        auto put_result = db_->Put( { tx_path }, serialized, { kSeedTopic } );
        ASSERT_TRUE( put_result.has_value() );

        crdt::GlobalDB::Buffer retire_value;
        retire_value.put( "retire-" + frontier->GetHash() );
        ASSERT_TRUE(
            db_->Put( { "/seed/certslot/" + frontier->GetHash() }, retire_value, { kSeedTopic } ).has_value() );

        ASSERT_TRUE(
            CertificateFallbackTestAccess::StoreTransactionCID( *tm_, tx_path, put_result.value().toString().value() )
                .has_value() );
    }

    // --- Burn-cleanup helpers (folded todo mintfunds-leaks-burn-utxo-on-consensus-reject) ---

    /// Seeds the validator registry once so signed proposals pass CheckProposal.
    void EnsureGenesisRegistryForBurnCleanup();

    /// Builds a signed local MintFunds-shaped mint-v2 (uncle_hash = burn ref string).
    std::shared_ptr<MintTransactionV2> MakeLocalBridgeMintV2( const base::Hash256 &burn,
                                                              uint64_t             nonce,
                                                              uint64_t             amount );

    /// Inserts + claims the burn UTXO exactly as MintFunds does.
    void SeedBurnUtxoAsMintFunds( const base::Hash256 &burn, uint64_t amount );

    /// True while the raw /utxo/<owner>/<burn>:0 record exists on disk.
    bool BurnRecordOnDisk( const base::Hash256 &burn ) const;

    /// Drives the REAL consensus reject (reject_and_maybe_fail_local -> FAILED).
    void RejectLocalTxThroughConsensus( const std::shared_ptr<GeniusTransaction> &tx );
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

    // The persisted slot certificate is ALSO delivered asynchronously: the CRDT
    // cert callback reconstructs the winner and the round timer confirms it via
    // OnConsensusCertificate. When that path tracks the tx first, the test's
    // synchronous FetchAndProcess is an idempotent no-op and CONFIRMED lands a
    // timer tick later (CI observed the winner VERIFYING one second before the
    // async confirm). Wait for the eventual state instead of racing it.
    ASSERT_TRUE( ::waitForCondition(
                     [&]()
                     {
                         const auto tracked = CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_,
                                                                                                 winner->GetHash() );
                         return tracked.has_value() &&
                                tracked->status == TransactionManager::TransactionStatus::CONFIRMED;
                     },
                     std::chrono::milliseconds( 10000 ),
                     nullptr ) )
        << "winner mint was not confirmed from its persisted slot certificate";

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

    // Park the round timer (handshake) before any durable certificate exists:
    // a residual timer tick dispatches durable slot certificates into the
    // TransactionManager handler on the timer thread via
    // RecoverPendingCertificateWork, racing this test's barrier choreography
    // and consuming the one-shot fail-next-store/hook sequence reserved for
    // the ingress threads. Observed as a 600s ctest timeout (2026-09-28) and,
    // with bounded barriers, as a fast stall whose log shows the dispatch
    // ("Failed to process certificate proposal_id=...") firing before the
    // ingress threads start (OSX Debug CI, 2026-09-30). The certificate is
    // built above under default timing, and every test constructs a fresh
    // manager, so this park is test-local.
    const auto manager = CertificateFallbackTestAccess::ConsensusManagerOf( *blockchain_ );
    ASSERT_TRUE( manager );
    CertificateFallbackTestAccess::ParkRoundTimer( manager );

    const auto             marker_key = std::string( "/bridge/executed/source-chain:" ) + winner->dag_st.uncle_hash();
    crdt::GlobalDB::Buffer marker_key_buffer;
    marker_key_buffer.put( marker_key );

    // Bound every barrier wait: a missed rendezvous must fail the test with a
    // diagnosis within this budget instead of hanging until the ctest timeout.
    constexpr auto kBarrierTimeout = std::chrono::seconds( 30 );

    auto                                                   &utxo_manager = account_->GetUTXOManager();
    std::mutex                                              barrier_mutex;
    std::condition_variable                                 barrier_cv;
    bool                                                    crdt_ready          = false;
    bool                                                    release_crdt        = false;
    bool                                                    first_store_entered = false;
    bool                                                    release_first_store = false;
    bool                                                    barrier_stalled     = false;
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
            if ( !barrier_cv.wait_for( lock, kBarrierTimeout, [&] { return release_crdt; } ) )
            {
                barrier_stalled = true;
            }
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
            if ( !barrier_cv.wait_for( lock, kBarrierTimeout, [&] { return release_first_store; } ) )
            {
                barrier_stalled = true;
            }
        } );

    /// Waits for @p what on the barrier, releasing both gates on timeout so
    /// the worker threads drain and the joins below complete. Returns false
    /// (and records the failure) instead of blocking until the ctest timeout.
    auto wait_for_barrier = [&]( auto predicate, const char *what ) -> bool
    {
        std::unique_lock lock( barrier_mutex );
        if ( barrier_cv.wait_for( lock, kBarrierTimeout, predicate ) )
        {
            return true;
        }
        release_crdt        = true;
        release_first_store = true;
        barrier_cv.notify_all();
        ADD_FAILURE() << "certificate ingress barrier stalled waiting for " << what;
        return false;
    };

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

    (void) wait_for_barrier( [&] { return crdt_ready; }, "CRDT ingress hook" );

    std::thread first_ingress(
        [&]
        {
            first_result = CertificateFallbackTestAccess::OnConsensusCertificate( *tm_,
                                                                                  winner->GetHash(),
                                                                                  certificate.value() );
        } );

    (void) wait_for_barrier( [&] { return first_store_entered; }, "certificate ingress PutUTXO hook" );

    // While the first insertion is still awaiting its durable snapshot, no
    // normal CRDT confirmation may create bridge completion evidence or a terminal state.
    EXPECT_TRUE( db_->GetDataStore()->get( marker_key_buffer ).has_error() );
    const auto in_flight = CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, winner->GetHash() );
    ASSERT_TRUE( in_flight.has_value() );
    EXPECT_NE( in_flight->status, TransactionManager::TransactionStatus::CONFIRMED );

    // Release the failing certificate ingress FIRST and join it before
    // releasing the CRDT path. ChangeTransactionState's CONFIRMED case reads
    // the effects_applied reservation when the CRDT path enters its decision
    // block; EnterFinalityFaultBarrier orders only the marker write, not that
    // decision. Releasing the CRDT path while the reservation still reads
    // effects_applied=true makes it skip ParseTransaction and confirm with
    // zero outputs (observed as a 0-UTXO flake at the final size assertion).
    // Letting the failed ingress finish first downgrades the reservation to
    // effects_applied=false, so the CRDT path deterministically observes the
    // retryable state and applies the effects itself.
    {
        std::lock_guard lock( barrier_mutex );
        release_first_store = true;
    }
    barrier_cv.notify_all();
    first_ingress.join();

    {
        std::lock_guard lock( barrier_mutex );
        release_crdt = true;
    }
    barrier_cv.notify_all();
    crdt_ingress.join();
    EXPECT_FALSE( barrier_stalled ) << "a barrier wait timed out; rendezvous was missed";
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

TEST_F( CertificateFallbackTest, CertifiedWinnerBeatsSignatureOnlyConfirmedConflict )
{
    /**
     * OnConsensusCertificate used to confirm the certified winner first and then
     * arbitrate with a content tie-break, so a locally tracked transaction that
     * CheckTransactionValidity had promoted to CONFIRMED on signature validity
     * alone could win BestHash and revert the certified winner — un-confirming
     * its applied effects and deleting it from the CRDT while every peer
     * confirmed it. A quorum-certified transaction must outrank any
     * non-certified CONFIRMED entry regardless of hash order; the tie-break
     * stays only for two genuinely certified finalists.
     */
    const auto make_tx = []( const std::string &source, uint64_t nonce, char burn_fill ) {
        SGTransaction::DAGStruct dag;
        dag.set_type( "mint-v2" );
        dag.set_source_addr( source );
        dag.set_nonce( nonce );
        dag.set_timestamp( static_cast<int64_t>( std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch() )
                               .count() ) );
        const auto burn_hash = base::Hash256::fromReadableString( std::string( 64, burn_fill ) );
        assert( burn_hash.has_value() );
        auto transaction = std::make_shared<MintTransactionV2>( MintTransactionV2::New( 42,
                                                                                        "source-chain",
                                                                                        kTestTokenId,
                                                                                        std::move( dag ),
                                                                                        { { burn_hash.value(), 0, {} } },
                                                                                        source ) );
        return transaction;
    };

    // Same nonce, different burns: conflicting by nonce+address, distinct slots.
    auto tx_a = make_tx( account_->GetAddress(), 80, 'b' );
    auto tx_b = make_tx( account_->GetAddress(), 80, 'c' );
    tx_a->MakeSignature( *account_ );
    tx_b->MakeSignature( *account_ );
    ASSERT_NE( tx_a->GetHash(), tx_b->GetHash() );

    // Assign roles so the tie-break always favors the signature-only conflict —
    // the exact direction in which the pre-fix arbitration reverted the
    // certified winner. (Hash order depends on the DAG timestamp, so the roles
    // are chosen at runtime rather than fixed.)
    const bool a_wins_tiebreak = blockchain_->BestHash( tx_a->GetHash(), tx_b->GetHash() ) == tx_a->GetHash();
    const auto conflict_tx     = a_wins_tiebreak ? tx_a : tx_b;
    const auto winner_tx       = a_wins_tiebreak ? tx_b : tx_a;
    ASSERT_EQ( blockchain_->BestHash( conflict_tx->GetHash(), winner_tx->GetHash() ),
               conflict_tx->GetHash() );

    const auto certificate = BuildSignedCertificate( winner_tx );
    ASSERT_TRUE( certificate.has_value() );
    PersistCertificateAtSlot( winner_tx->GetSlotID(), certificate.value() );

    // The node tracks its own competing transaction and promotes it to CONFIRMED
    // on signature validity alone (no certificate exists for its slot).
    ASSERT_TRUE( FetchAndProcess( conflict_tx ).has_value() );
    ASSERT_TRUE( CertificateFallbackTestAccess::CheckTransactionValidity( *tm_, { 80 } ).has_value() );
    {
        const auto tracked = CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, conflict_tx->GetHash() );
        ASSERT_TRUE( tracked.has_value() );
        ASSERT_EQ( tracked->status, TransactionManager::TransactionStatus::CONFIRMED );
    }

    // The quorum-certified winner arrives for the same nonce. Track it first so
    // the handler runs the tracked-transaction path (with its arbitration)
    // rather than the standalone-validator embedded fallback.
    ASSERT_TRUE( FetchAndProcess( winner_tx ).has_value() );
    const auto result = CertificateFallbackTestAccess::OnConsensusCertificate( *tm_,
                                                                               winner_tx->GetHash(),
                                                                               certificate.value() );
    ASSERT_TRUE( result.has_value() );

    const auto winner_tracked = CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, winner_tx->GetHash() );
    ASSERT_TRUE( winner_tracked.has_value() );
    EXPECT_EQ( winner_tracked->status, TransactionManager::TransactionStatus::CONFIRMED );

    const auto conflict_tracked = CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, conflict_tx->GetHash() );
    ASSERT_TRUE( conflict_tracked.has_value() );
    EXPECT_EQ( conflict_tracked->status, TransactionManager::TransactionStatus::FAILED );
}

TEST_F( CertificateFallbackTest, DuplicateBurnMintRefusedWhenSiblingConsumedOutpoint )
{
    /**
     * The last line of the duplicate-burn defense: when a mint's burn input is
     * already CONSUMED, a sibling mint for the same burn applied its effects —
     * positive proof of duplication that no check-then-act race can fake. The
     * parse path used to warn and create the outputs anyway, minting one
     * verified burn twice. It must now refuse before any output exists.
     */
    const auto burn = base::Hash256::fromReadableString( std::string( 64, 'f' ) ).value();
    auto mint_tx = std::make_shared<MintTransactionV2>( MintTransactionV2::New(
        7,
        "public",
        kTestTokenId,
        []
        {
            SGTransaction::DAGStruct dag;
            dag.set_type( "mint-v2" );
            dag.set_source_addr( "duplicate-burn-owner" );
            dag.set_nonce( 81 );
            dag.set_timestamp( static_cast<int64_t>( std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::system_clock::now().time_since_epoch() )
                                   .count() ) );
            return dag;
        }(),
        { { burn, 0, {} } },
        "duplicate-burn-owner" ) );
    mint_tx->MakeSignature( *account_ );

    // The sibling mint already consumed the burn outpoint.
    auto &utxo_mgr = account_->GetUTXOManager();
    ASSERT_TRUE( utxo_mgr
                     .PutUTXO( GeniusUTXO( burn, 0, 7, kTestTokenId ),
                               "duplicate-burn-owner",
                               UTXOManager::UTXOType::UTXO_BRIDGE )
                     .has_value() );
    InputUTXOInfo sibling_input;
    sibling_input.txid_hash_  = burn;
    sibling_input.output_idx_ = 0;
    ASSERT_TRUE(
        utxo_mgr.ConsumeUTXOs( { sibling_input }, "duplicate-burn-owner", UTXOManager::UTXOType::UTXO_BRIDGE )
            .has_value() );

    const uint64_t balance_before = utxo_mgr.GetBalance();

    // The duplicate's parse must fail without creating outputs. Parse directly:
    // arrival processing queues the parse for TickOnce, which this fixture does
    // not run.
    const auto result = CertificateFallbackTestAccess::ParseTransaction( *tm_, mint_tx );
    EXPECT_TRUE( result.has_error() );

    const uint64_t balance_after = utxo_mgr.GetBalance();
    EXPECT_EQ( balance_after, balance_before );
}

TEST_F( CertificateFallbackTest, RepeatedHandlerFailuresEngageBackoffLease )
{
    /**
     * A handler failing once or twice is transient (storage glitch) and must
     * retry on the next tick exactly as before — the fault choreography
     * depends on it. A handler failing persistently re-runs full certificate
     * validation plus itself on every 500ms tick forever — a CPU/log loop — so
     * after the fast-retry threshold the stall must carry a live backoff lease.
     */
    const auto winner = MakeCompetingMintV2( account_->GetAddress(), 130 );
    ASSERT_TRUE( winner );
    const auto certificate = BuildSignedCertificate( winner );
    ASSERT_TRUE( certificate.has_value() );
    PersistCertificateAtSlot( winner->GetSlotID(), certificate.value() );

    const auto manager = CertificateFallbackTestAccess::ConsensusManagerOf( *blockchain_ );
    ASSERT_TRUE( manager );
    const auto key = "/cert/" + winner->GetSlotID();
    crdt::GlobalDB::Buffer callback_value;
    callback_value.put( certificate.value().SerializeAsString() );
    CertificateFallbackTestAccess::CertificateReceived(
        manager, crdt::CRDTCallbackManager::NewDataPair{ key, std::move( callback_value ) } );

    // Sticky marker failure: every dispatch fails at the handler.
    CertificateFallbackTestAccess::SetBridgeExecutedMarkerWriteFailure( *tm_, true );

    // Early failures stay on the fast path: Stalled with no live lease.
    for ( int i = 0; i < 4; ++i )
    {
        CertificateFallbackTestAccess::RecoverPendingCertificateWork( manager );
        ASSERT_TRUE( CertificateFallbackTestAccess::HasCertificateWorkState(
            manager, key, crdt::CRDTWorkJournal::State::Stalled ) );
        ASSERT_FALSE( CertificateFallbackTestAccess::HasLiveBackoffLease( manager, key ) );
    }

    // Past the fast-retry threshold the stall carries a live backoff lease.
    for ( int i = 0; i < 6; ++i )
    {
        CertificateFallbackTestAccess::RecoverPendingCertificateWork( manager );
    }
    EXPECT_TRUE( CertificateFallbackTestAccess::HasCertificateWorkState(
        manager, key, crdt::CRDTWorkJournal::State::Stalled ) );
    EXPECT_TRUE( CertificateFallbackTestAccess::HasLiveBackoffLease( manager, key ) );

    CertificateFallbackTestAccess::SetBridgeExecutedMarkerWriteFailure( *tm_, false );
}

// ---------------------------------------------------------------------------
// Cert-slot previous_hash resolution (17-03 / GATE-04 / SC-3)
// ---------------------------------------------------------------------------

/**
 * SC-3 case 1: with the node's own frontier transaction physically erased
 * (EraseTransactionEntry -- the real guarded erase, not a hand-deleted key)
 * and its certificate present at the slot, GetOutgoingPreviousHash resolves
 * the frontier hash from cert-slot data. Pre-erase, the tracked rung answers
 * first (ladder order preserved); post-erase, only the cert rung can.
 */
TEST_F( CertificateFallbackTest, CertSlotPreviousHash_PrunedFrontierResolvesFromCert )
{
    constexpr uint64_t kFrontierNonce = 41;
    const auto         frontier       = MakeTransfer( account_->GetAddress(), kFrontierNonce );
    ASSERT_FALSE( frontier->GetHash().empty() );

    SeedErasableFrontierEntry( frontier );

    const auto certificate = BuildSignedCertificate( frontier );
    ASSERT_TRUE( certificate.has_value() );
    ASSERT_TRUE( TransactionManager::CertificateMatchesTransaction( certificate.value(), *frontier ) );

    // Park the round timer AFTER the certificate is built (registry settling
    // needs default timing): with the timer parked, no async dispatch tracks
    // the frontier from the durable cert, so after the erase the resolution
    // provably comes from the cert rung, not from re-tracked state.
    CertificateFallbackTestAccess::ParkRoundTimer( CertificateFallbackTestAccess::ConsensusManagerOf( *blockchain_ ) );
    PersistCertificateAtSlot( frontier->GetSlotID(), certificate.value() );

    // Warm pre-prune state: the frontier is tracked (CONFIRMED -- its cert is
    // durable) and the tracked rung answers while history is intact.
    ASSERT_TRUE( FetchAndProcess( frontier ).has_value() );
    EXPECT_EQ( CertificateFallbackTestAccess::GetOutgoingPreviousHash( *tm_, kFrontierNonce + 1 ),
               frontier->GetHash() );
    EXPECT_TRUE( CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, frontier->GetHash() ).has_value() );

    // The physical erase: value, block bytes, map entry, and tracked state.
    ASSERT_TRUE( tm_->EraseTransactionEntry( frontier->GetHash() ).has_value() );
    EXPECT_TRUE( db_->Get( TransactionManager::GetTransactionPath( *frontier ) ).has_error() );
    EXPECT_FALSE( CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, frontier->GetHash() ).has_value() );

    // Post-prune: the cert at /cert/<addr>:<nonce> still binds the chain link.
    EXPECT_EQ( CertificateFallbackTestAccess::GetOutgoingPreviousHash( *tm_, kFrontierNonce + 1 ),
               frontier->GetHash() );
}

/**
 * SC-3 case 2: cold start after the prune. Nothing was ever tracked, no
 * local confirmed head exists (the fresh-manager state a restart into a
 * pruned store presents), the frontier value is physically gone -- the nonce
 * chain still resolves through the certificate, exactly frontier nonce + 1.
 */
TEST_F( CertificateFallbackTest, CertSlotPreviousHash_ColdStartNonceChainAfterPrune )
{
    constexpr uint64_t kFrontierNonce = 42;
    const auto         frontier       = MakeTransfer( account_->GetAddress(), kFrontierNonce );
    ASSERT_FALSE( frontier->GetHash().empty() );

    SeedErasableFrontierEntry( frontier );

    const auto certificate = BuildSignedCertificate( frontier );
    ASSERT_TRUE( certificate.has_value() );

    CertificateFallbackTestAccess::ParkRoundTimer( CertificateFallbackTestAccess::ConsensusManagerOf( *blockchain_ ) );
    PersistCertificateAtSlot( frontier->GetSlotID(), certificate.value() );

    // Cold-start preconditions: no tracked state, no persisted head.
    EXPECT_FALSE( CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, frontier->GetHash() ).has_value() );
    EXPECT_TRUE( account_->GetLocalConfirmedTxHash( kFrontierNonce ).has_error() );

    ASSERT_TRUE( tm_->EraseTransactionEntry( frontier->GetHash() ).has_value() );
    EXPECT_TRUE( account_->GetLocalConfirmedTxHash( kFrontierNonce ).has_error() );

    // The chain link resolves from the certificate alone.
    EXPECT_EQ( CertificateFallbackTestAccess::GetOutgoingPreviousHash( *tm_, kFrontierNonce + 1 ),
               frontier->GetHash() );
    // The derived nonce chain is exactly frontier -> frontier+1: below the
    // frontier no certificate exists, so resolution fails closed there.
    EXPECT_EQ( CertificateFallbackTestAccess::GetOutgoingPreviousHash( *tm_, kFrontierNonce ), "" );
}

/**
 * SC-3 case 3 (negative control): frontier value erased WITHOUT a
 * certificate at the slot -- resolution must fail closed to the empty
 * string, never a guessed hash.
 */
TEST_F( CertificateFallbackTest, CertSlotPreviousHash_NoCertNoValueFailsClosed )
{
    constexpr uint64_t kFrontierNonce = 43;
    const auto         frontier       = MakeTransfer( account_->GetAddress(), kFrontierNonce );
    ASSERT_FALSE( frontier->GetHash().empty() );

    SeedErasableFrontierEntry( frontier );
    // Deliberately NO certificate at /cert/<addr>:<nonce>.

    ASSERT_TRUE( tm_->EraseTransactionEntry( frontier->GetHash() ).has_value() );
    EXPECT_TRUE( db_->Get( TransactionManager::GetTransactionPath( *frontier ) ).has_error() );

    EXPECT_EQ( CertificateFallbackTestAccess::GetOutgoingPreviousHash( *tm_, kFrontierNonce + 1 ), "" );
}

/**
 * SC-3 case 4 (Pitfall 7 equivalence): for a certified NON-registration
 * predecessor with history intact, rungs 1-2 miss by construction (fresh
 * manager, timer parked: nothing tracked, no local confirmed head), so the
 * ladder's answer IS the cert rung's -- and it must equal what the full
 * CRDT scan (rung 4) finds for the same predecessor.
 */
TEST_F( CertificateFallbackTest, CertSlotPreviousHash_CertRungMatchesCRDTScan )
{
    constexpr uint64_t kFrontierNonce = 44;
    const auto         frontier       = MakeTransfer( account_->GetAddress(), kFrontierNonce );
    ASSERT_FALSE( frontier->GetHash().empty() );

    SeedErasableFrontierEntry( frontier );

    const auto certificate = BuildSignedCertificate( frontier );
    ASSERT_TRUE( certificate.has_value() );

    CertificateFallbackTestAccess::ParkRoundTimer( CertificateFallbackTestAccess::ConsensusManagerOf( *blockchain_ ) );
    PersistCertificateAtSlot( frontier->GetSlotID(), certificate.value() );

    // Capture the cert rung DIRECTLY, not only through the ladder: with
    // history intact the value-dependent rung 4 (CRDT scan) returns the same
    // hash, so a ladder-level assertion alone could pass while the rung
    // itself regressed (the same hash from a different source). The direct
    // capture binds the rung; the ladder assert below then proves the
    // composed order answers too.
    const auto cert_rung_answer = CertificateFallbackTestAccess::GetRegisteredOutgoingPreviousHash(
        *tm_,
        kFrontierNonce + 1 );
    ASSERT_EQ( cert_rung_answer, frontier->GetHash() );
    EXPECT_EQ( CertificateFallbackTestAccess::GetOutgoingPreviousHash( *tm_, kFrontierNonce + 1 ),
               frontier->GetHash() );

    // Pitfall 7 equivalence: the full CRDT scan (rung 4) finds the same hash
    // for the certified non-registration predecessor.
    EXPECT_EQ( CertificateFallbackTestAccess::QueryOutgoingPreviousHashFromCRDT( *tm_, kFrontierNonce + 1 ),
               frontier->GetHash() );
}

/**
 * SC-3 case 5 (regression): a registration-type predecessor still resolves
 * through its slot certificate -- the pre-17-03 behavior is not regressed by
 * the generalized rung. Registration values live at reg/{addr}, never under
 * tx/, so the certificate was always the only authority for this link.
 */
TEST_F( CertificateFallbackTest, CertSlotPreviousHash_RegistrationRungStillWorks )
{
    constexpr uint64_t kRegistrationNonce = 45;

    SGTransaction::DAGStruct dag;
    dag.set_type( "registration" );
    dag.set_source_addr( account_->GetAddress() );
    dag.set_nonce( kRegistrationNonce );
    dag.set_timestamp( static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
            .count() ) );

    const auto registration = std::make_shared<RegistrationTransaction>(
        RegistrationTransaction::New( account_->GetAddress(),
                                      1,
                                      SGTransaction::RegistrationMetadata{},
                                      std::move( dag ) ) );
    ASSERT_FALSE( registration->GetHash().empty() );
    ASSERT_EQ( registration->GetType(), "registration" );

    const auto certificate = BuildSignedCertificate( registration );
    ASSERT_TRUE( certificate.has_value() );

    CertificateFallbackTestAccess::ParkRoundTimer( CertificateFallbackTestAccess::ConsensusManagerOf( *blockchain_ ) );
    PersistCertificateAtSlot( registration->GetSlotID(), certificate.value() );

    EXPECT_EQ( CertificateFallbackTestAccess::GetOutgoingPreviousHash( *tm_, kRegistrationNonce + 1 ),
               registration->GetHash() );
}

// ---------------------------------------------------------------------------
// Burn-UTXO cleanup on consensus-rejected local mints (folded todo
// mintfunds-leaks-burn-utxo-on-consensus-reject, live incident 2026-10-20)
// ---------------------------------------------------------------------------

namespace
{
    /// Nonce base for the BurnCleanup cases (avoids every other case's range).
    constexpr uint64_t kBurnCleanupNonceBase = 210;
}

void CertificateFallbackTest::EnsureGenesisRegistryForBurnCleanup()
{
    auto registry = blockchain_->GetValidatorRegistry();
    ASSERT_NE( registry, nullptr );
    ASSERT_FALSE( registry
                      ->StoreGenesisRegistry( { account_->GetAddress() },
                                              [account = account_]( std::vector<uint8_t> payload )
                                              { return account->Sign( std::move( payload ) ); } )
                      .has_error() );
    ASSERT_TRUE( ::waitForCondition(
        [&registry]()
        {
            auto current = registry->LoadCurrentRegistry();
            return current.has_value() && !registry->GetRegistryCid().empty();
        },
        std::chrono::milliseconds( 2000 ),
        nullptr ) );
}

std::shared_ptr<MintTransactionV2> CertificateFallbackTest::MakeLocalBridgeMintV2( const base::Hash256 &burn,
                                                                                   uint64_t             nonce,
                                                                                   uint64_t             amount )
{
    // MintFunds shape (TransactionManager::MintFunds): FillDAGStruct(transaction_hash)
    // carries the burn ref string as dag.uncle_hash -- the SAME string MintFunds
    // claims the burn outpoint under (the D-18 reservation-id contract).
    SGTransaction::DAGStruct dag;
    dag.set_type( "mint-v2" );
    dag.set_source_addr( account_->GetAddress() );
    dag.set_nonce( nonce );
    dag.set_uncle_hash( burn.toReadableString() );
    dag.set_timestamp( static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
            .count() ) );

    auto mint = std::make_shared<MintTransactionV2>( MintTransactionV2::New( amount,
                                                                             "public",
                                                                             kTestTokenId,
                                                                             std::move( dag ),
                                                                             { { burn, 0, {} } },
                                                                             account_->GetAddress() ) );
    mint->MakeSignature( *account_ );
    return mint;
}

void CertificateFallbackTest::SeedBurnUtxoAsMintFunds( const base::Hash256 &burn, uint64_t amount )
{
    // The exact MintFunds burn seeding: insert the UTXO_BRIDGE burn entry, then
    // atomically claim it under the burn ref string.
    auto &utxo_mgr = account_->GetUTXOManager();
    ASSERT_TRUE( utxo_mgr
                     .PutUTXO( GeniusUTXO( burn, 0, amount, kTestTokenId, account_->GetAddress() ),
                               account_->GetAddress(),
                               UTXOManager::UTXOType::UTXO_BRIDGE )
                     .has_value() );
    ASSERT_EQ( utxo_mgr.TryReserveOutpoint( burn, 0, burn.toReadableString(), UTXOManager::UTXOType::UTXO_BRIDGE ),
               UTXOManager::OutpointClaim::kClaimed );
}

bool CertificateFallbackTest::BurnRecordOnDisk( const base::Hash256 &burn ) const
{
    // The raw persisted record key (UTXOManager's /utxo/<owner>/<txid>:<idx>).
    crdt::GlobalDB::Buffer key_buffer;
    key_buffer.put( "/utxo/" + account_->GetAddress() + "/" + burn.toReadableString() + ":0" );
    return db_->GetDataStore()->get( key_buffer ).has_value();
}

void CertificateFallbackTest::RejectLocalTxThroughConsensus( const std::shared_ptr<GeniusTransaction> &tx )
{
    EnsureGenesisRegistryForBurnCleanup();

    auto signing_manager = ConsensusManager::New(
        blockchain_->GetValidatorRegistry(),
        db_,
        pubs_,
        [account = account_]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); },
        account_->GetAddress() );
    ASSERT_NE( signing_manager, nullptr );

    // The subject nonce deliberately mismatches the embedded tx nonce: the
    // deterministic consensus reject. (The live incident's reject reason was the
    // witness/type-rules leg; both legs fire the SAME reject_and_maybe_fail_local
    // -> ChangeTransactionState(FAILED) trigger, and the cleanup branch is
    // reason-agnostic.)
    auto subject = ConsensusManager::CreateNonceSubject( account_->GetAddress(),
                                                         tx->GetNonce() + 1,
                                                         tx->GetHash(),
                                                         tx->SerializeToEmbeddedTransaction(),
                                                         std::nullopt,
                                                         std::nullopt );
    ASSERT_TRUE( subject.has_value() );
    auto proposal = signing_manager->CreateProposal( subject.value(),
                                                     account_->GetAddress(),
                                                     blockchain_->GetValidatorRegistry()->GetRegistryCid(),
                                                     blockchain_->GetValidatorRegistry()->GetRegistryEpoch() );
    ASSERT_TRUE( proposal.has_value() );

    CertificateFallbackTestAccess::HandleProposal( CertificateFallbackTestAccess::ConsensusManagerOf( *blockchain_ ),
                                                   proposal.value() );
    signing_manager->Close();
}

/**
 * Folded todo (live incident 2026-10-20, staging net): a locally-originated
 * bridge mint rejected by consensus left the MintFunds-inserted UTXO_BRIDGE
 * burn entry dangling -- READY in memory AND on disk (it survived restart via
 * LoadUTXOs and wedged boot). The FAILED transition's mint-v2 rollback only
 * RELEASES the reservation (RESERVED -> READY, memory-only): it never removes
 * the inserted entry. After the real consensus-reject -> FAILED transition,
 * the burn entry must be GONE everywhere and nothing may linger under the
 * burn-ref reservation id.
 */
TEST_F( CertificateFallbackTest, BurnCleanup_LocalMintRejectedByConsensusRemovesBurnUtxoAndReservation )
{
    const auto burn     = base::Hash256::fromReadableString( std::string( 64, 'b' ) ).value();
    const auto mint     = MakeLocalBridgeMintV2( burn, kBurnCleanupNonceBase, 10000 );
    auto      &utxo_mgr = account_->GetUTXOManager();

    SeedBurnUtxoAsMintFunds( burn, 10000 );

    // Present-before (three-part assertion, part 1): the incident's starting
    // state -- burn entry inserted and RESERVED, record durable on disk.
    ASSERT_TRUE( utxo_mgr.IsOutPointReserved( burn, 0 ) );
    ASSERT_TRUE( BurnRecordOnDisk( burn ) );

    // The REAL trigger: HandleProposal -> HandleNonceConsensusSubject ->
    // reject_and_maybe_fail_local -> ChangeTransactionState(FAILED).
    ASSERT_NO_FATAL_FAILURE( RejectLocalTxThroughConsensus( mint ) );

    const auto tracked = CertificateFallbackTestAccess::GetTrackedTxByHash( *tm_, mint->GetHash() );
    ASSERT_TRUE( tracked.has_value() );
    ASSERT_EQ( tracked->status, TransactionManager::TransactionStatus::FAILED );

    // Absent-after, in memory: no UTXO_BRIDGE entry at the burn outpoint.
    EXPECT_EQ( utxo_mgr.GetOutPointState( burn, 0 ), std::nullopt );

    // Absent-after, on disk: the live-incident observable was the record that
    // survived restart ("stays on disk in READY state").
    EXPECT_FALSE( BurnRecordOnDisk( burn ) );

    // No lingering reservation under the burn-ref id: a fresh claim for the
    // same burn starts from a clean slate (kNotClaimable = unknown outpoint),
    // so a relayer redelivery can re-mint instead of wedging.
    EXPECT_EQ( utxo_mgr.TryReserveOutpoint( burn, 0, "post-cleanup-probe", UTXOManager::UTXOType::UTXO_BRIDGE ),
               UTXOManager::OutpointClaim::kNotClaimable );
}
