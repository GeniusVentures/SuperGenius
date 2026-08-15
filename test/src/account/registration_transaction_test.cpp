/**
 * @file       registration_transaction_test.cpp
 * @brief      Unit tests for RegistrationTransaction — factory, serialization round-trip, topics,
 *             filter gates, and end-to-end registration flow.
 * @date       2026-07-15
 * @author     (Phase 4)
 */
#include <gtest/gtest.h>

#include <boost/filesystem/operations.hpp>
#include <cassert>
#include <chrono>
#include <optional>
#include <string>
#include <thread>

#include "account/GeniusAccount.hpp"
#include "account/MigrationAllowList.hpp"
#include "account/RegistrationTransaction.hpp"
#include "account/RevokeTransaction.hpp"
#include "account/TransactionManager.hpp"
#include "account/TransferTransaction.hpp"
#include "base/hexutil.hpp"
#include "blockchain/Blockchain.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/ConsensusAuth.hpp"
#include "crypto/hasher.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/wait_condition.hpp"

namespace
{
    using namespace sgns;

    /// Test token identifier.
    const sgns::TokenID kTestTokenId = sgns::TokenID::FromBytes( { 0x00 } );

    /// Fixture that provides MemorySecureStorage so GeniusAccount can be created without
    /// interacting with the OS keychain.
    class RegistrationTransactionTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                { return std::make_shared<MemorySecureStorage>( identifier ); } );
            path_ = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
        }

        void TearDown() override
        {
            GeniusAccount::SetSecureStorageFactory( nullptr );
            boost::filesystem::remove_all( path_ );
        }

        boost::filesystem::path path_;
    };

} // namespace

// ---------------------------------------------------------------------------
// RoundTripSerialization — SerializeByteVector → DeSerializeByteVector
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionTest, RoundTripSerialization )
{
    // Build a well-formed DAG struct with a 128-hex source address.
    SGTransaction::DAGStruct dag;
    dag.set_type( "registration" );
    dag.set_source_addr( std::string( 128, 'a' ) );
    dag.set_nonce( 0 );
    dag.set_timestamp( 1721000000 );

    // Build metadata with all optional fields populated.
    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "test_game" );
    metadata.set_publisher_id( "test_publisher" );
    metadata.set_dev_wallet( std::string( 128, 'd' ) );
    metadata.set_peers_cut( 5000 );

    std::string main_address( 128, 'b' ); // 128-hex main pubkey

    auto tx = RegistrationTransaction::New( main_address, 1, metadata, dag );

    // Serialize and deserialize.
    auto  serialized = tx.SerializeByteVector();
    auto  deserialized = RegistrationTransaction::DeSerializeByteVector( serialized );
    ASSERT_NE( deserialized, nullptr );

    EXPECT_EQ( deserialized->GetMainAddress(), main_address );
    EXPECT_EQ( deserialized->GetSequence(), 1 );
    EXPECT_EQ( deserialized->GetMetadata().game_id(), "test_game" );
    EXPECT_EQ( deserialized->GetMetadata().publisher_id(), "test_publisher" );
    EXPECT_EQ( deserialized->GetMetadata().dev_wallet(), std::string( 128, 'd' ) );
    EXPECT_EQ( deserialized->GetMetadata().peers_cut(), 5000 );
}

// ---------------------------------------------------------------------------
// FactoryFillHash — New() calls FillHash; GetHash is non-empty and GetType is "registration"
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionTest, FactoryFillHash )
{
    SGTransaction::DAGStruct dag;
    dag.set_type( "registration" );
    dag.set_source_addr( std::string( 128, 'c' ) );
    dag.set_nonce( 0 );

    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "factory_test" );

    auto tx = RegistrationTransaction::New( std::string( 128, 'm' ), 42, metadata, dag );

    EXPECT_FALSE( tx.GetHash().empty() );
    EXPECT_EQ( tx.GetType(), "registration" );
}

// ---------------------------------------------------------------------------
// SerializeToEmbeddedTransaction — oneof registration() set correctly
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionTest, SerializeToEmbeddedTransaction )
{
    SGTransaction::DAGStruct dag;
    dag.set_type( "registration" );
    dag.set_source_addr( std::string( 128, 'e' ) );
    dag.set_nonce( 7 );

    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "embedded_test" );

    std::string main_address( 128, 'f' );
    auto        tx = RegistrationTransaction::New( main_address, 99, metadata, dag );

    auto embedded = tx.SerializeToEmbeddedTransaction();

    // The registration oneof arm should be set.
    EXPECT_EQ( embedded.transaction_case(), EmbeddedTransaction::kRegistration );
    EXPECT_EQ( embedded.registration().main_address(), main_address );
    EXPECT_EQ( embedded.registration().sequence(), 99 );
    EXPECT_EQ( embedded.registration().metadata().game_id(), "embedded_test" );
}

// ---------------------------------------------------------------------------
// GetTopics — includes main_address_
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionTest, GetTopics )
{
    SGTransaction::DAGStruct dag;
    dag.set_type( "registration" );
    dag.set_source_addr( std::string( 128, 'g' ) );
    dag.set_nonce( 0 );

    SGTransaction::RegistrationMetadata metadata;
    std::string                        main_address( 128, 'h' );

    auto tx = RegistrationTransaction::New( main_address, 1, metadata, dag );

    auto topics = tx.GetTopics();
    EXPECT_NE( topics.find( main_address ), topics.end() );
}

// ===================================================================
// E2E Integration Tests (CRDT-backed fixture)
// ===================================================================

namespace sgns
{
    /**
     * @brief Friend accessor for private TransactionManager methods needed by E2E tests.
     */
    class RegistrationE2ETestAccess
    {
    public:
        static std::shared_ptr<GeniusTransaction> GetTransactionByHash(
            TransactionManager &tm,
            const std::string  &hash )
        {
            return tm.GetTransactionByHash( hash );
        }

        static std::optional<std::vector<crdt::pb::Element>> FilterRegistration(
            TransactionManager      &tm,
            const crdt::pb::Element &element )
        {
            return tm.FilterRegistration( element );
        }

        static outcome::result<std::vector<RegistrationDiscoveryEntry>> GetRegistrationsForMain(
            TransactionManager &tm,
            const std::string  &main_address )
        {
            return tm.GetRegistrationsForMain( main_address );
        }
    };
} // namespace sgns

namespace
{
    using namespace sgns;

    /**
     * @brief CRDT-backed fixture for end-to-end registration transaction tests.
     *
     * Creates a GeniusAccount, Blockchain, and TransactionManager using the
     * CRDT test infrastructure (no GeniusNode, no network sync).
     */
    class RegistrationTransactionE2ETest : public ::test::CRDTFixture
    {
    public:
        RegistrationTransactionE2ETest()
            : CRDTFixture( "reg_tx_e2e_test" )
        {
            GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                { return std::make_shared<MemorySecureStorage>( identifier ); } );

            account_ = GeniusAccount::New( kTestTokenId, base_path / "account" );
            assert( account_ != nullptr );

            auto load_result = account_->GetUTXOManager().LoadUTXOs( db_->GetDataStore() );
            assert( load_result.has_value() );

            // Second bare identity (D-60/D-65, Phase 3 Plan 04) — the "child" used by
            // CONS-01/CONS-02/REGR-01/02/03 tests. No own TransactionManager, per
            // 03-RESEARCH.md's Open Question #1 recommendation: the gates under test
            // (CheckParentChildAuthority/CheckTransactionAuthorization) are invoked directly
            // on whatever tx object is passed to them, regardless of which node constructed it.
            child_account_ = GeniusAccount::New( kTestTokenId, base_path / "child_account" );
            assert( child_account_ != nullptr );

            auto child_load_result = child_account_->GetUTXOManager().LoadUTXOs( db_->GetDataStore() );
            assert( child_load_result.has_value() );

            // Make account_ the self-authorized genesis validator so EnsureValidatorRegistry()
            // (called synchronously inside Blockchain::New) actually establishes a real,
            // single-validator registry with a non-empty registry CID — required for
            // CertifyChildRegistration's CreateConsensusProposal/SubmitProposal recipe to reach
            // quorum and produce a real certificate (D-65/D-26). Without this, account_'s address
            // never matches the default DEFAULT_FULL_NODE_PUB_ADDRESS, EnsureValidatorRegistry()
            // is a no-op, and every proposal is rejected for having an empty registry_cid —
            // pre-existing tests never noticed because they never checked CheckCertificate.
            Blockchain::SetAuthorizedFullNodeAddress( account_->GetAddress() );

            blockchain_ = Blockchain::New(
                db_, account_, pubs_, []( outcome::result<void> ) {} );
            assert( blockchain_ != nullptr );

            constexpr auto kTimestampTolerance = std::chrono::milliseconds( 300000 );
            constexpr auto kMutabilityWindow   = std::chrono::milliseconds( 600000 );

            tm_ = TransactionManager::New(
                db_, io_, account_,
                blockchain_,
                true,  // full_node — enables isolated boot in CheckNonce() when FetchNetworkNonce fails
                0,     // subnet_id
                kTimestampTolerance,
                kMutabilityWindow );
            assert( tm_ != nullptr );

            // Run the io_context on a worker thread so TickOnce() (self-reposting) drives the
            // TransactionManager state machine through INITIALIZING → READY.
            work_guard_.emplace( boost::asio::make_work_guard( *io_ ) );
            io_thread_  = std::thread( [this]() { io_->run(); } );
        }

        void TearDown() override
        {
            if ( tm_ )
                tm_->Stop();
            work_guard_.reset();
            if ( io_thread_.joinable() )
                io_thread_.join();
            GeniusAccount::SetSecureStorageFactory( nullptr );
        }

        /**
         * @brief Seeds `account_` (main) with a REAL, spendable, traceable UTXO via
         *        TransactionManager::MigrationFunds — required so ordinary transfer/recovery
         *        tests (CONS-01/CONS-02/REGR-01/02) have funds to move.
         * @details Two earlier approaches were tried and rejected during implementation:
         *          (1) seeding via a directly-`PutUTXO`-injected synthetic UTXO (fake all-zero
         *          txid, no real producing transaction) — `TransferFunds`'s `SendTransactionItem`
         *          step calls `BuildUTXOWitness`, which requires the input's producing
         *          transaction to actually exist (`GetTransactionByHash`) so it can extract
         *          produced-output leaves and build a merkle inclusion proof — "Missing producer
         *          transaction" otherwise. (2) `TransactionManager::MintFunds` — this is a real
         *          bridge-style mint requiring `PublicChainInputValidator`'s RPC-backed smart-
         *          contract verification (`VerifyPublicChainSmartContract`), which this bare test
         *          fixture has no RPC endpoint configured for; the mint's own consensus witness
         *          validation fails, its bridge "burn" placeholder UTXO gets rolled back to READY
         *          via `RevertMintTransaction`, and that bogus placeholder (not a real producer)
         *          then gets accidentally re-selected by a later ordinary `TransferFunds`,
         *          reproducing the exact same "missing producer" failure one step removed.
         *          `MigrationFunds` avoids both: `MigrationInputValidator::RequiresConsensusUTXOData()`
         *          returns false, so `SendTransactionItem` never calls `BuildUTXOWitness` for it at
         *          all, and eligibility is a purely local/CRDT-based `MigrationAllowList` check
         *          (D-65-scoped; requires no RPC). Once genuinely CONFIRMED, its output becomes a
         *          normal, `GetTransactionByHash`-traceable producer for later ordinary spends.
         * @param[in] amount Amount to migrate to main's own address.
         * @param[in] migration_version Unique per-call migration namespace — MigrationTransaction
         *            derives a one-time claim key from (migration_version, address, token_id), so
         *            each call within the same test MUST use a distinct value.
         */
        void MintMainFunds( uint64_t amount, const std::string &migration_version )
        {
            MigrationAllowList allow_list( db_->GetDataStore(), migration_version );
            auto               store_result = allow_list.StoreObservedBalance( account_->GetAddress(), amount );
            ASSERT_TRUE( store_result.has_value() ) << "Seeding the migration allow-list should succeed";

            // Capture the baseline BEFORE submitting so a second call within the same test (e.g.
            // CONS-01's two independent mints) waits for its OWN increment to actually land,
            // rather than exiting immediately because a PRIOR mint already satisfies `>= amount`.
            const uint64_t baseline = account_->GetUTXOManager().GetBalance( kTestTokenId, account_->GetAddress() );
            const uint64_t target   = baseline + amount;

            auto migrate_result = tm_->MigrationFunds( amount, migration_version, kTestTokenId, "" );
            ASSERT_TRUE( migrate_result.has_value() ) << "Migrating main's funds should succeed";
            std::chrono::milliseconds elapsed;
            ASSERT_WAIT_FOR_CONDITION( ([this, target]() { return account_->GetUTXOManager().GetBalance( kTestTokenId, account_->GetAddress() ) >= target; }),
                std::chrono::milliseconds( 10000 ),
                "Main's migrated balance should be visible within timeout",
                &elapsed );
        }

        /**
         * @brief Builds a child-signed RegistrationTransaction under `account_`'s address,
         *        certifies it via a manually-assembled consensus proposal (single-validator
         *        self-vote, since this fixture's Blockchain::New already established a genesis
         *        registry with account_ as sole validator), then writes it directly into the
         *        CRDT at the same reg/{child_addr} key format RegisterChild/FilterRegistration
         *        already use (TransactionManager.cpp:618-619, :2873-2874). Polls
         *        Blockchain::CheckCertificate until the round-timer thread (already running
         *        since Blockchain::New) processes the certificate (D-26).
         * @param[in] sequence Registration sequence number.
         * @param[in] metadata Registration metadata.
         * @return The constructed, certified RegistrationTransaction. Callers MUST immediately
         *         assert `blockchain_->CheckCertificate(child_reg.GetHash())` — this helper uses
         *         non-fatal EXPECT_* internally since it returns a value (ASSERT_* requires a
         *         void-returning function).
         */
        RegistrationTransaction CertifyChildRegistration( uint64_t                             sequence,
                                                           SGTransaction::RegistrationMetadata metadata )
        {
            SGTransaction::DAGStruct dag;
            dag.set_type( "registration" );
            dag.set_source_addr( child_account_->GetAddress() );
            dag.set_nonce( 0 );
            // A real (non-zero, current) timestamp is required — this tx now goes through the
            // REAL ValidateTransactionForConsensus pipeline (CheckTransactionWellFormed/
            // CheckTransactionTimestamp), unlike the pre-existing unit tests in this file that
            // never submit their manually-built DAGStructs for actual consensus validation.
            dag.set_timestamp( std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch() )
                                    .count() );

            auto reg_tx = RegistrationTransaction::New( account_->GetAddress(), sequence, metadata, dag );
            reg_tx.MakeSignature( *child_account_ );

            CertifySignedRegistrationTx( reg_tx, *child_account_ );

            return reg_tx;
        }

        /**
         * @brief Certifies an already-built, already-signed RegistrationTransaction — shared tail
         *        extracted from CertifyChildRegistration (Phase 5 Plan 05) so a manually-built
         *        lifecycle-change RegistrationTransaction (e.g. a post-Revoke re-registration,
         *        which CertifyChildRegistration's own 2-arg signature can't express since it
         *        never carries detach_flag/supersedes_sequence/a caller-chosen main_address) can
         *        be certified through the exact same recipe, without duplicating the
         *        CreateConsensusNonceSubject/manual-proposal/SubmitProposal/poll-CheckCertificate
         *        logic a second time.
         *
         * Submits a manually-assembled consensus proposal signed by @p signer, waits for the
         * certificate, then writes @p reg_tx directly into the CRDT at
         * reg/{reg_tx.GetSrcAddress()} (the CRDT write is deferred past certification so
         * FilterRegistration's gate 3b sees the pre-change stored sequence — D-38 / D-70).
         *
         * Per this fixture's registration convention, every RegistrationTx — initial or
         * lifecycle-change — is always signed by the registering child itself (D-04/D-05/D-37),
         * so @p signer is always child_account_ in practice; kept as a parameter rather than
         * hard-coded so this helper's contract stays explicit about whose signature the
         * proposal_id/signature fields need).
         *
         * Uses non-fatal EXPECT_* internally (void-returning function). Callers MUST immediately
         * assert `blockchain_->CheckCertificate(reg_tx.GetHash())`.
         * @param[in] reg_tx Already child-signed RegistrationTransaction to certify.
         * @param[in] signer The identity that signed reg_tx — signs the consensus proposal too.
         */
        void CertifySignedRegistrationTx( const RegistrationTransaction &reg_tx, GeniusAccount &signer )
        {
            // Certify by manually assembling and SIGNER-signing a consensus proposal, then
            // submitting it through main's blockchain_ for automatic self-voting/certification.
            //
            // NOTE (deviation from the plan's literal wording — discovered during
            // implementation, not assumed): Blockchain::CreateConsensusProposal cannot be used
            // here. It always signs the proposal with THIS blockchain_ instance's own bound
            // signer (account_'s/main's key, fixed at Blockchain::New construction), while also
            // using its single account_id parameter for TWO independent purposes: (1) the nonce
            // subject's account_id, which TransactionManager::HandleNonceConsensusSubject
            // requires to equal the embedded transaction's own GetSrcAddress() (the child's
            // address, for a child-signed registration) — a data-integrity check — and (2) the
            // proposal's proposer_id, which ConsensusManager::CheckProposal verifies the
            // proposal's signature against. Passing the child's address satisfies (1) but fails
            // (2) (signed by main, not child — confirmed via this exact rejection during
            // implementation: "signature verification failed proposer_id=<child_addr>"); passing
            // main's address satisfies (2) but fails (1) ("Account mismatch" — also directly
            // observed). These two checks are only simultaneously satisfiable if the proposal is
            // genuinely signed by the same identity that signed reg_tx. The fix: build the
            // Subject via the public static Blockchain::CreateConsensusNonceSubject, then
            // assemble and sign the Proposal by hand (mirroring ConsensusManager::CreateProposal's
            // exact field-population and ID-derivation sequence, which is private and not
            // directly callable), signing with signer.Sign(...) so proposer_id genuinely matches
            // the signing key. The automatic self-VOTE that follows (cast by main, the sole
            // registered validator, via SubmitProposal's default self_vote=true) is unaffected by
            // this — voting and proposing are independent signing operations, and main's vote
            // alone reaches quorum in this single-validator registry regardless of who proposed.
            auto embedded_tx    = reg_tx.SerializeToEmbeddedTransaction();
            auto subject_result = Blockchain::CreateConsensusNonceSubject( reg_tx.GetSrcAddress(),
                                                                            reg_tx.GetNonce(),
                                                                            reg_tx.GetHash(),
                                                                            embedded_tx,
                                                                            std::nullopt,
                                                                            std::nullopt );
            EXPECT_TRUE( subject_result.has_value() ) << "CreateConsensusNonceSubject should succeed";

            if ( subject_result.has_value() )
            {
                ConsensusManager::Proposal proposal;
                *proposal.mutable_subject() = subject_result.value();
                proposal.set_proposer_id( reg_tx.GetSrcAddress() );
                proposal.set_registry_cid( blockchain_->GetValidatorRegistry()->GetRegistryCid() );
                proposal.set_registry_epoch( blockchain_->GetValidatorRegistry()->GetRegistryEpoch() );
                proposal.set_timestamp( std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::system_clock::now().time_since_epoch() )
                                             .count() );

                // Proposal ID: sha256 of the signing bytes with proposal_id itself still empty
                // (mirrors ConsensusManager::CreateProposalId's private implementation).
                auto id_signing_bytes = ProposalSigningBytes( proposal );
                EXPECT_TRUE( id_signing_bytes.has_value() ) << "ProposalSigningBytes (for ID) should succeed";
                if ( id_signing_bytes.has_value() )
                {
                    auto id_hash = crypto::sha2_256( id_signing_bytes.value().data(), id_signing_bytes.value().size() );
                    proposal.set_proposal_id(
                        base::hex_lower( gsl::span<const uint8_t>( id_hash.data(), id_hash.size() ) ) );
                }

                auto signing_bytes = ProposalSigningBytes( proposal );
                EXPECT_TRUE( signing_bytes.has_value() ) << "ProposalSigningBytes should succeed";
                if ( signing_bytes.has_value() )
                {
                    auto signature = signer.Sign( signing_bytes.value() ); // matches proposer_id
                    proposal.set_signature( signature.data(), signature.size() );
                }

                auto submit_result = blockchain_->SubmitProposal( proposal );
                EXPECT_TRUE( submit_result.has_value() ) << "SubmitProposal should succeed";
            }

            EXPECT_WAIT_FOR_CONDITION( ([this, &reg_tx]() { return blockchain_->CheckCertificate( reg_tx.GetHash() ); }),
                std::chrono::milliseconds( 25000 ),
                "Registration should be certified within timeout",
                nullptr );

            // Write the registration into the CRDT at reg/{src_addr} AFTER the certificate is
            // confirmed. Deferring the CRDT write past certification ensures FilterRegistration's
            // gate 3b (D-38: supersedes_sequence == stored_sequence fork-prevention) sees the
            // pre-change stored sequence, not the new one — critical for lifecycle-change
            // registrations (Detach/Replace-Main/re-registration) where a premature CRDT write
            // would advance the stored sequence and cause a self-inflicted tombstone (D-70).
            std::string            reg_key = TransactionManager::GetBlockChainBase() + "reg/" + reg_tx.GetSrcAddress();
            auto                   serialized = reg_tx.SerializeByteVector();
            base::Buffer           buffer( std::vector<uint8_t>( serialized.begin(), serialized.end() ) );
            crdt::HierarchicalKey  hk( reg_key );
            auto                   put_result = db_->Put( hk, buffer, {} );
            EXPECT_TRUE( put_result.has_value() ) << "Registration CRDT Put should succeed";
        }

        std::shared_ptr<GeniusAccount>      account_;
        std::shared_ptr<GeniusAccount>      child_account_;
        std::shared_ptr<Blockchain>         blockchain_;
        std::shared_ptr<TransactionManager> tm_;

        std::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_guard_;
        std::thread io_thread_;
    };

} // namespace

// ---------------------------------------------------------------------------
// ChildRegistrationEndToEnd — verifies the full RegisterChild flow
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, ChildRegistrationEndToEnd )
{
    // Start the TransactionManager and poll for READY state.
    // TickOnce() (self-reposting on the io_context worker thread) drives the state machine
    // through INITIALIZING → InitTransactions → READY. CheckNonce() blocks ~5s on network
    // nonce fetch, so allow generous timeout.
    tm_->Start();

    ASSERT_WAIT_FOR_CONDITION(
        [this]() { return tm_->GetState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 60000 ),
        "TransactionManager did not reach READY state within 60 s",
        nullptr );

    // Registration data
    std::string                        main_address( 128, 'a' ); // 128-hex main pubkey
    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "e2e_test_game" );

    // Register child via TransactionManager
    auto result = tm_->RegisterChild( main_address, metadata, 1 );
    ASSERT_TRUE( result.has_value() );
    std::string tx_hash = result.value();
    EXPECT_FALSE( tx_hash.empty() );

    // Query the transaction by hash
    auto tx = RegistrationE2ETestAccess::GetTransactionByHash( *tm_, tx_hash );
    ASSERT_NE( tx, nullptr );

    // Verify status is SENDING (processed asynchronously by TickOnce READY branch — poll briefly)
    EXPECT_WAIT_FOR_CONDITION( ([this, &tx_hash]() { return tm_->GetTransactionStatusByTxId( tx_hash ) == TransactionManager::TransactionStatus::SENDING; }),
        std::chrono::milliseconds( 10000 ),
        "Transaction should reach SENDING status",
        nullptr );

    // Downcast to RegistrationTransaction
    auto reg_tx = std::dynamic_pointer_cast<RegistrationTransaction>( tx );
    ASSERT_NE( reg_tx, nullptr );

    // Verify transaction fields
    EXPECT_EQ( reg_tx->GetType(), "registration" );
    EXPECT_EQ( reg_tx->GetMainAddress(), main_address );
    EXPECT_EQ( reg_tx->GetSequence(), 1 );
    EXPECT_EQ( reg_tx->GetMetadata().game_id(), "e2e_test_game" );

    // Verify DAG struct fields
    // Nonce starts at 0 for a fresh account with no prior transactions.
    EXPECT_EQ( reg_tx->GetNonce(), 0ULL );
    EXPECT_FALSE( reg_tx->GetSrcAddress().empty() );
    EXPECT_EQ( reg_tx->GetSrcAddress(), account_->GetAddress() );
}

// ---------------------------------------------------------------------------
// ChildRegistrationTamperedSignatureRejected — per D-45: tampered sig fails
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionTest, ChildRegistrationTamperedSignatureRejected )
{
    // Create GeniusAccount (real keypair)
    auto account = GeniusAccount::New( kTestTokenId, path_ / "sig_acct" );
    ASSERT_NE( account, nullptr );

    // Build a valid RegistrationTx with the account's actual address as source_addr
    SGTransaction::DAGStruct dag;
    dag.set_type( "registration" );
    dag.set_source_addr( account->GetAddress() );
    dag.set_nonce( 0 );

    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "sig_test" );

    std::string main_address( 128, 'm' );

    auto tx = RegistrationTransaction::New( main_address, 1, metadata, dag );

    // MakeSignature modifies dag_st in-place with the signature.
    // After signing, dag_st and the internal proto are updated.
    tx.MakeSignature( *account );

    // Verify the signature is currently valid
    EXPECT_TRUE( tx.CheckSignature() )
        << "Valid signature should pass CheckSignature";

    // Now tamper the DAG struct's signature by modifying it post-signing.
    // Serialize to get a clean snapshot, then modify the signature in the
    // proto payload.
    auto serialized = tx.SerializeByteVector();

    // Deserialize and modify the DAG signature directly in the proto struct
    SGTransaction::RegistrationTx tx_struct;
    ASSERT_TRUE( tx_struct.ParseFromArray( serialized.data(), serialized.size() ) )
        << "Should parse valid serialized RegistrationTx";

    // Tamper the DAG signature — flip the last byte of the signature
    auto *dag_mutable = tx_struct.mutable_dag_struct();
    std::string sig = dag_mutable->signature();
    ASSERT_FALSE( sig.empty() ) << "Signature should be non-empty after signing";
    sig[sig.size() - 1] ^= 0xFF;
    dag_mutable->set_signature( sig );

    // Re-serialize the tampered proto
    size_t size = tx_struct.ByteSizeLong();
    std::vector<uint8_t> tampered_bytes( size );
    ASSERT_TRUE( tx_struct.SerializeToArray( tampered_bytes.data(), tampered_bytes.size() ) );

    // Deserialize the tampered bytes
    auto deserialized = RegistrationTransaction::DeSerializeByteVector( tampered_bytes );
    ASSERT_NE( deserialized, nullptr )
        << "Tampered protobuf should still parse (only signature field modified)";

    // CheckSignature should detect the tampered signature
    EXPECT_FALSE( deserialized->CheckSignature() )
        << "Tampered signature should fail CheckSignature verification";
}

// ---------------------------------------------------------------------------
// FilterRegistrationAcceptsValid — valid RegistrationTx passes filter
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, FilterRegistrationAcceptsValid )
{
    // Build a well-formed RegistrationTx
    SGTransaction::DAGStruct dag;
    dag.set_type( "registration" );
    dag.set_source_addr( account_->GetAddress() );
    dag.set_nonce( 0 );

    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "filter_test" );

    std::string main_address( 128, 'v' ); // 128-hex (valid)

    auto tx = RegistrationTransaction::New( main_address, 1, metadata, dag );
    tx.MakeSignature( *account_ );

    // Serialize the tx
    auto serialized = tx.SerializeByteVector();

    // Construct a CRDT element in the reg/ namespace
    crdt::pb::Element element;
    element.set_key( "/bc/999/reg/" + account_->GetAddress() );
    element.set_value( std::string( serialized.begin(), serialized.end() ) );

    // Call FilterRegistration via accessor
    auto result = RegistrationE2ETestAccess::FilterRegistration( *tm_, element );

    // Valid registration should be accepted (nullopt)
    EXPECT_FALSE( result.has_value() )
        << "Valid RegistrationTx should be accepted (std::nullopt)";
}

// ---------------------------------------------------------------------------
// FilterRegistrationRejectsBadMainAddress — malformed main_address gets tombstone
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, FilterRegistrationRejectsBadMainAddress )
{
    // Build a RegistrationTx with a malformed main_address (not 128 hex chars)
    SGTransaction::DAGStruct dag;
    dag.set_type( "registration" );
    dag.set_source_addr( account_->GetAddress() );
    dag.set_nonce( 0 );

    SGTransaction::RegistrationMetadata metadata;

    // main_address is only 64 chars — not a valid 128-hex public key
    std::string bad_main_address( 64, 'b' );

    auto tx = RegistrationTransaction::New( bad_main_address, 1, metadata, dag );
    tx.MakeSignature( *account_ );

    // Serialize
    auto serialized = tx.SerializeByteVector();

    // Construct CRDT element
    crdt::pb::Element element;
    element.set_key( "/bc/999/reg/" + account_->GetAddress() );
    element.set_value( std::string( serialized.begin(), serialized.end() ) );

    // Call FilterRegistration via accessor
    auto result = RegistrationE2ETestAccess::FilterRegistration( *tm_, element );

    // Malformed main_address should be rejected (tombstone)
    EXPECT_TRUE( result.has_value() )
        << "RegistrationTx with malformed main_address should be tombstoned";
}

// ---------------------------------------------------------------------------
// FilterRegistrationRejectsTamperedSignature — tampered sig gets tombstone
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, FilterRegistrationRejectsTamperedSignature )
{
    // Build a valid RegistrationTx
    SGTransaction::DAGStruct dag;
    dag.set_type( "registration" );
    dag.set_source_addr( account_->GetAddress() );
    dag.set_nonce( 0 );

    SGTransaction::RegistrationMetadata metadata;
    std::string                        main_address( 128, 't' );

    auto tx = RegistrationTransaction::New( main_address, 1, metadata, dag );
    tx.MakeSignature( *account_ );

    // Serialize and tamper the DAG signature at proto level (deterministic)
    auto serialized = tx.SerializeByteVector();

    SGTransaction::RegistrationTx tx_struct;
    ASSERT_TRUE( tx_struct.ParseFromArray( serialized.data(), serialized.size() ) )
        << "Should parse valid serialized RegistrationTx";

    auto *dag_mutable = tx_struct.mutable_dag_struct();
    std::string sig = dag_mutable->signature();
    ASSERT_FALSE( sig.empty() ) << "Signature should be non-empty after signing";
    sig[sig.size() - 1] ^= 0xFF;
    dag_mutable->set_signature( sig );

    size_t size = tx_struct.ByteSizeLong();
    std::vector<uint8_t> tampered_bytes( size );
    ASSERT_TRUE( tx_struct.SerializeToArray( tampered_bytes.data(), tampered_bytes.size() ) );

    // Construct CRDT element with tampered bytes
    crdt::pb::Element element;
    element.set_key( "/bc/999/reg/" + account_->GetAddress() );
    element.set_value( std::string( tampered_bytes.begin(), tampered_bytes.end() ) );

    // Call FilterRegistration via accessor — should reject with tombstone
    auto result = RegistrationE2ETestAccess::FilterRegistration( *tm_, element );

    EXPECT_TRUE( result.has_value() )
        << "RegistrationTx with tampered signature should be tombstoned";
}

// ---------------------------------------------------------------------------
// Gate (d): FilterRegistrationRejectsZeroSequence
// sequence == 0 is always rejected as a well-formed check
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, FilterRegistrationRejectsZeroSequence )
{
    SGTransaction::DAGStruct dag;
    dag.set_type( "registration" );
    dag.set_source_addr( account_->GetAddress() );
    dag.set_nonce( 0 );

    SGTransaction::RegistrationMetadata metadata;
    std::string main_address( 128, 'z' ); // 128-hex

    auto tx = RegistrationTransaction::New( main_address, 0, metadata, dag );
    tx.MakeSignature( *account_ );

    auto serialized = tx.SerializeByteVector();

    crdt::pb::Element element;
    element.set_key( "/bc/999/reg/" + account_->GetAddress() );
    element.set_value( std::string( serialized.begin(), serialized.end() ) );

    auto result = RegistrationE2ETestAccess::FilterRegistration( *tm_, element );

    // Zero sequence should always be rejected (tombstone)
    EXPECT_TRUE( result.has_value() )
        << "RegistrationTx with sequence=0 should be tombstoned";
}

// ---------------------------------------------------------------------------
// Gate (d): FilterRegistrationRejectsNonMonotonicSequence
// Pre-populate CRDT with sequence=4, then test sequence=3 → tombstone
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, FilterRegistrationRejectsNonMonotonicSequence )
{
    // Build a RegistrationTx with sequence=4 to pre-populate CRDT
    SGTransaction::DAGStruct dag4;
    dag4.set_type( "registration" );
    dag4.set_source_addr( account_->GetAddress() );
    dag4.set_nonce( 0 );

    SGTransaction::RegistrationMetadata metadata4;
    std::string main_address( 128, 'm' );

    auto existing_tx = RegistrationTransaction::New( main_address, 4, metadata4, dag4 );
    existing_tx.MakeSignature( *account_ );

    // Write directly to GlobalDB at the reg/ key
    std::string reg_key = TransactionManager::GetBlockChainBase() + "reg/" + account_->GetAddress();
    auto serialized_existing = existing_tx.SerializeByteVector();
    base::Buffer existing_buffer( std::vector<uint8_t>( serialized_existing.begin(), serialized_existing.end() ) );
    crdt::HierarchicalKey hk( reg_key );
    auto put_result = db_->Put( hk, existing_buffer, {} );
    ASSERT_TRUE( put_result.has_value() ) << "Pre-population Put should succeed";

    // Now create a new element with sequence=3 (lower than stored 4)
    SGTransaction::DAGStruct dag3;
    dag3.set_type( "registration" );
    dag3.set_source_addr( account_->GetAddress() );
    dag3.set_nonce( 0 );

    SGTransaction::RegistrationMetadata metadata3;
    auto new_tx = RegistrationTransaction::New( main_address, 3, metadata3, dag3 );
    new_tx.MakeSignature( *account_ );

    auto serialized_new = new_tx.SerializeByteVector();

    crdt::pb::Element element;
    element.set_key( "/bc/999/reg/" + account_->GetAddress() );
    element.set_value( std::string( serialized_new.begin(), serialized_new.end() ) );

    auto result = RegistrationE2ETestAccess::FilterRegistration( *tm_, element );

    // Non-monotonic sequence should be rejected (tombstone)
    EXPECT_TRUE( result.has_value() )
        << "RegistrationTx with sequence=3 <= stored=4 should be tombstoned";
}

// ---------------------------------------------------------------------------
// Gate (d): FilterRegistrationAcceptsHigherSequence
// Pre-populate CRDT with sequence=4, then test sequence=5 → accepted
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, FilterRegistrationAcceptsHigherSequence )
{
    // Build a RegistrationTx with sequence=4 to pre-populate CRDT
    SGTransaction::DAGStruct dag4;
    dag4.set_type( "registration" );
    dag4.set_source_addr( account_->GetAddress() );
    dag4.set_nonce( 0 );

    SGTransaction::RegistrationMetadata metadata4;
    std::string main_address( 128, 'h' );

    auto existing_tx = RegistrationTransaction::New( main_address, 4, metadata4, dag4 );
    existing_tx.MakeSignature( *account_ );

    // Write directly to GlobalDB at the reg/ key
    std::string reg_key = TransactionManager::GetBlockChainBase() + "reg/" + account_->GetAddress();
    auto serialized_existing = existing_tx.SerializeByteVector();
    base::Buffer existing_buffer( std::vector<uint8_t>( serialized_existing.begin(), serialized_existing.end() ) );
    crdt::HierarchicalKey hk( reg_key );
    auto put_result = db_->Put( hk, existing_buffer, {} );
    ASSERT_TRUE( put_result.has_value() ) << "Pre-population Put should succeed";

    // Now create a new element with sequence=5 (higher than stored 4)
    SGTransaction::DAGStruct dag5;
    dag5.set_type( "registration" );
    dag5.set_source_addr( account_->GetAddress() );
    dag5.set_nonce( 0 );

    SGTransaction::RegistrationMetadata metadata5;
    auto new_tx = RegistrationTransaction::New( main_address, 5, metadata5, dag5 );
    new_tx.MakeSignature( *account_ );

    auto serialized_new = new_tx.SerializeByteVector();

    crdt::pb::Element element;
    element.set_key( "/bc/999/reg/" + account_->GetAddress() );
    element.set_value( std::string( serialized_new.begin(), serialized_new.end() ) );

    auto result = RegistrationE2ETestAccess::FilterRegistration( *tm_, element );

    // Higher sequence should be accepted (nullopt)
    EXPECT_FALSE( result.has_value() )
        << "RegistrationTx with sequence=5 > stored=4 should be accepted";
}

// ---------------------------------------------------------------------------
// RegisterChildAutoDeriveSequence — 2-arg overload auto-derives from CRDT
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, RegisterChildAutoDeriveFirstRegistration )
{
    // Start TM and wait for READY (needed for RegisterChild)
    tm_->Start();

    ASSERT_WAIT_FOR_CONDITION(
        [this]() { return tm_->GetState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 60000 ),
        "TransactionManager did not reach READY",
        nullptr );

    std::string main_address( 128, 'a' );
    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "auto_derive_test" );

    // 2-arg call — should auto-derive sequence=1 (first registration)
    auto result = tm_->RegisterChild( main_address, metadata );
    ASSERT_TRUE( result.has_value() );
    std::string tx_hash = result.value();
    EXPECT_FALSE( tx_hash.empty() );

    // Verify the transaction has sequence=1
    auto tx = RegistrationE2ETestAccess::GetTransactionByHash( *tm_, tx_hash );
    ASSERT_NE( tx, nullptr );
    auto reg_tx = std::dynamic_pointer_cast<RegistrationTransaction>( tx );
    ASSERT_NE( reg_tx, nullptr );
    EXPECT_EQ( reg_tx->GetSequence(), 1 );
}

TEST_F( RegistrationTransactionE2ETest, RegisterChildAutoDeriveIncrementsFromStored )
{
    // Start TM and wait for READY
    tm_->Start();

    ASSERT_WAIT_FOR_CONDITION(
        [this]() { return tm_->GetState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 60000 ),
        "TransactionManager did not reach READY",
        nullptr );

    std::string main_address( 128, 'b' );
    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "auto_derive_test" );

    // First: register with explicit sequence=5 using 3-arg overload
    auto result1 = tm_->RegisterChild( main_address, metadata, 5 );
    ASSERT_TRUE( result1.has_value() );

    // Poll for SENDING to ensure CRDT is committed
    EXPECT_WAIT_FOR_CONDITION( ([this, &result1]() { return tm_->GetTransactionStatusByTxId( result1.value() ) == TransactionManager::TransactionStatus::SENDING; }),
        std::chrono::milliseconds( 10000 ),
        "CRDT should be committed and status should reach SENDING",
        nullptr );

    // Second: 2-arg call — should auto-derive sequence=6 (stored=5 + 1)
    auto result2 = tm_->RegisterChild( main_address, metadata );
    ASSERT_TRUE( result2.has_value() );

    auto tx = RegistrationE2ETestAccess::GetTransactionByHash( *tm_, result2.value() );
    ASSERT_NE( tx, nullptr );
    auto reg_tx = std::dynamic_pointer_cast<RegistrationTransaction>( tx );
    ASSERT_NE( reg_tx, nullptr );
    EXPECT_EQ( reg_tx->GetSequence(), 6 );
}

TEST_F( RegistrationTransactionE2ETest, RegisterChildPreservesCallerSequence )
{
    // Start TM and wait for READY
    tm_->Start();

    ASSERT_WAIT_FOR_CONDITION(
        [this]() { return tm_->GetState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 60000 ),
        "TransactionManager did not reach READY",
        nullptr );

    std::string main_address( 128, 'c' );
    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "preserve_seq_test" );

    // 3-arg call with explicit sequence
    auto result = tm_->RegisterChild( main_address, metadata, 5 );
    ASSERT_TRUE( result.has_value() );

    auto tx = RegistrationE2ETestAccess::GetTransactionByHash( *tm_, result.value() );
    ASSERT_NE( tx, nullptr );
    auto reg_tx = std::dynamic_pointer_cast<RegistrationTransaction>( tx );
    ASSERT_NE( reg_tx, nullptr );
    EXPECT_EQ( reg_tx->GetSequence(), 5 );
}

// ---------------------------------------------------------------------------
// GetRegistrationsForMain — discovery read path tests (Phase 05-02 Task 1)
// ---------------------------------------------------------------------------

TEST_F( RegistrationTransactionE2ETest, GetRegistrationsForMainReturnsEmptyForNoChildren )
{
    // Start TM and wait for READY
    tm_->Start();

    ASSERT_WAIT_FOR_CONDITION(
        [this]() { return tm_->GetState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 60000 ),
        "TransactionManager did not reach READY",
        nullptr );

    std::string main_address( 128, 'd' );
    auto result = RegistrationE2ETestAccess::GetRegistrationsForMain( *tm_, main_address );
    ASSERT_TRUE( result.has_value() );
    EXPECT_TRUE( result.value().empty() )
        << "Expected empty vector when no children registered to this main";
}

TEST_F( RegistrationTransactionE2ETest, GetRegistrationsForMainReturnsMatchingEntries )
{
    // Start TM and wait for READY
    tm_->Start();

    ASSERT_WAIT_FOR_CONDITION(
        [this]() { return tm_->GetState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 60000 ),
        "TransactionManager did not reach READY",
        nullptr );

    std::string main_address( 128, 'e' );
    SGTransaction::RegistrationMetadata metadata1;
    metadata1.set_game_id( "discovery_test_1" );
    SGTransaction::RegistrationMetadata metadata2;
    metadata2.set_game_id( "discovery_test_2" );

    // Register two children under the same main
    auto result1 = tm_->RegisterChild( main_address, metadata1, 1 );
    ASSERT_TRUE( result1.has_value() );
    auto result2 = tm_->RegisterChild( main_address, metadata2, 2 );
    ASSERT_TRUE( result2.has_value() );

    // Allow time for CRDT processing
    ASSERT_WAIT_FOR_CONDITION( ([this, &main_address]() {
            auto entries = RegistrationE2ETestAccess::GetRegistrationsForMain( *tm_, main_address );
            return entries.has_value() && !entries.value().empty();
        }),
        std::chrono::milliseconds( 5000 ),
        "Registrations for main should appear in CRDT",
        nullptr );

    auto entries = RegistrationE2ETestAccess::GetRegistrationsForMain( *tm_, main_address );
    ASSERT_TRUE( entries.has_value() );
    EXPECT_GE( entries.value().size(), 1 )
        << "Expected at least one registration entry for this main";
    // Verify each entry has the correct main_addr
    for ( const auto &entry : entries.value() )
    {
        EXPECT_EQ( entry.main_addr, main_address );
        EXPECT_FALSE( entry.child_addr.empty() );
    }
}

TEST_F( RegistrationTransactionE2ETest, GetRegistrationsForMainFiltersByMainAddress )
{
    // Start TM and wait for READY
    tm_->Start();

    ASSERT_WAIT_FOR_CONDITION(
        [this]() { return tm_->GetState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 60000 ),
        "TransactionManager did not reach READY",
        nullptr );

    std::string main_a( 128, 'f' );
    std::string main_b( 128, 'g' );
    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "filter_test" );

    // Register a child under main_a
    auto result = tm_->RegisterChild( main_a, metadata, 1 );
    ASSERT_TRUE( result.has_value() );

    // Allow time for CRDT processing
    ASSERT_WAIT_FOR_CONDITION( ([this, &main_a]() {
            auto entries = RegistrationE2ETestAccess::GetRegistrationsForMain( *tm_, main_a );
            return entries.has_value() && !entries.value().empty();
        }),
        std::chrono::milliseconds( 5000 ),
        "Registrations for main_a should appear in CRDT",
        nullptr );

    // Query main_a — should find the registration
    auto entries_a = RegistrationE2ETestAccess::GetRegistrationsForMain( *tm_, main_a );
    ASSERT_TRUE( entries_a.has_value() );
    EXPECT_GE( entries_a.value().size(), 1 );

    // Query main_b — should be empty (no child registered to it)
    auto entries_b = RegistrationE2ETestAccess::GetRegistrationsForMain( *tm_, main_b );
    ASSERT_TRUE( entries_b.has_value() );
    EXPECT_TRUE( entries_b.value().empty() )
        << "Expected empty vector for main_b — no child registered to it";
}

// ===================================================================
// Phase 3 Plan 04 — CONS-01/CONS-02/D-21/REGR-01/02/03 end-to-end tests
// against the real consensus pipeline (CheckParentChildAuthority,
// CheckTransactionAuthorization).
// ===================================================================

// ---------------------------------------------------------------------------
// MainFundsChildApprovedByGate — CONS-01: an ordinary main-signed transfer to
// a would-be-child address, and to an arbitrary unregistered address, both
// succeed unchanged. No registration state exists in this test at all.
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, MainFundsChildApprovedByGate )
{
    tm_->Start();

    ASSERT_WAIT_FOR_CONDITION(
        [this]() { return tm_->GetState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 60000 ),
        "TransactionManager did not reach READY",
        nullptr );

    // Fund main with two independent spendable UTXOs (two separate migration transactions, each
    // under its own migration_version namespace since MigrationTransaction derives a one-time
    // claim key from (migration_version, address, token_id)) so both TransferFunds calls below
    // can each select a fresh READY UTXO without racing on the first call's reservation.
    constexpr uint64_t kMintAmount = 1000;
    MintMainFunds( kMintAmount, "cons01_migration_1" );
    MintMainFunds( kMintAmount, "cons01_migration_2" );

    constexpr uint64_t kAmount = 100;

    // Ordinary transfer to a would-be-child address — nothing registered in this test;
    // CONS-01 requires zero registration state.
    auto result_to_child_addr = tm_->TransferFunds( kAmount, child_account_->GetAddress(), kTestTokenId );
    ASSERT_TRUE( result_to_child_addr.has_value() )
        << "Main-signed transfer to an unregistered would-be-child address should succeed unchanged";

    // Ordinary transfer to an arbitrary unregistered address.
    auto result_to_arbitrary = tm_->TransferFunds( kAmount, std::string( 128, 'z' ), kTestTokenId );
    ASSERT_TRUE( result_to_arbitrary.has_value() )
        << "Main-signed transfer to an arbitrary unregistered address should succeed unchanged";
}

// ---------------------------------------------------------------------------
// MainRecoversFromChildApproved — CONS-02: a certified child's funds can be
// recovered by main, and both CheckParentChildAuthority and
// CheckTransactionAuthorization independently approve the resulting tx.
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, MainRecoversFromChildApproved )
{
    tm_->Start();

    ASSERT_WAIT_FOR_CONDITION(
        [this]() { return tm_->GetState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 60000 ),
        "TransactionManager did not reach READY",
        nullptr );

    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "cons02_test" );
    auto child_reg = CertifyChildRegistration( 1, metadata );
    ASSERT_TRUE( blockchain_->CheckCertificate( child_reg.GetHash() ) )
        << "Child registration must be certified before recovery can be attempted";

    MintMainFunds( 1000, "cons02_migration" );

    constexpr uint64_t kFundAmount    = 500;
    constexpr uint64_t kRecoverAmount = 200;

    auto fund_result = tm_->TransferFunds( kFundAmount, child_account_->GetAddress(), kTestTokenId );
    ASSERT_TRUE( fund_result.has_value() ) << "Main should be able to fund the certified child";

    // Poll for the child's UTXO to appear — the node's own TickOnce READY-branch ingests its
    // own submitted transaction and calls PutProducedUTXOs on first CRDT observation.
    ASSERT_WAIT_FOR_CONDITION( ([this, &kFundAmount]() { return account_->GetUTXOManager().GetBalance( kTestTokenId, child_account_->GetAddress() ) >= kFundAmount; }),
        std::chrono::milliseconds( 10000 ),
        "Child's funded balance should be visible within timeout",
        nullptr );

    auto recover_result = tm_->RecoverFromChild( child_account_->GetAddress(), kRecoverAmount, kTestTokenId );
    ASSERT_TRUE( recover_result.has_value() ) << "Main should be able to recover funds from the certified child";

    auto tx = RegistrationE2ETestAccess::GetTransactionByHash( *tm_, recover_result.value() );
    ASSERT_NE( tx, nullptr );

    EXPECT_TRUE( tm_->CheckParentChildAuthority( *tx ) )
        << "CheckParentChildAuthority should approve a main-signed recovery to the certified main address";
    EXPECT_TRUE( tm_->CheckTransactionAuthorization( *tx ) )
        << "CheckTransactionAuthorization should approve a main-signed recovery via the certified-main branch";
}

// ---------------------------------------------------------------------------
// MainRecoveryWrongDestinationRejected — D-21: a main-signed, certified-
// child-sourced transaction with a destination other than the certified main
// address is rejected by CheckParentChildAuthority specifically (not by
// CheckTransactionAuthorization).
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, MainRecoveryWrongDestinationRejected )
{
    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "d21_test" );
    auto child_reg = CertifyChildRegistration( 1, metadata );
    ASSERT_TRUE( blockchain_->CheckCertificate( child_reg.GetHash() ) )
        << "Child registration must be certified for the D-21 destination check to even apply";

    SGTransaction::DAGStruct dag;
    dag.set_type( "transfer" );
    dag.set_source_addr( child_account_->GetAddress() );
    dag.set_nonce( 0 );

    // Empty inputs — CheckParentChildAuthority/CheckTransactionAuthorization never inspect
    // input ownership, only the whole-tx signature and the primary output's destination.
    std::vector<InputUTXOInfo>  inputs;
    std::vector<OutputDestInfo> outputs{ { 100, std::string( 128, 'q' ), kTestTokenId } }; // NOT account_'s address

    auto tx = std::make_shared<TransferTransaction>( TransferTransaction::New( inputs, outputs, dag ) );
    tx->MakeSignature( *account_ ); // main signs — this is what makes it "delegated", not child-self-signed

    EXPECT_FALSE( tm_->CheckParentChildAuthority( *tx ) )
        << "D-21: a delegated recovery to a destination other than the certified main address must be rejected";
    EXPECT_TRUE( tm_->CheckTransactionAuthorization( *tx ) )
        << "The rejection above must be the destination-restriction gate, not a signature failure";
}

// ---------------------------------------------------------------------------
// ChildTransferToArbitraryAndMainUnaffected — REGR-01: child-self-signed
// transfers to an arbitrary address and to its own registered main both pass
// unconditionally (no destination restriction applies to the child's own key).
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, ChildTransferToArbitraryAndMainUnaffected )
{
    tm_->Start();

    ASSERT_WAIT_FOR_CONDITION(
        [this]() { return tm_->GetState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 60000 ),
        "TransactionManager did not reach READY",
        nullptr );

    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "regr01_test" );
    auto child_reg = CertifyChildRegistration( 1, metadata );
    ASSERT_TRUE( blockchain_->CheckCertificate( child_reg.GetHash() ) );

    MintMainFunds( 1000, "regr01_migration" );

    constexpr uint64_t kFundAmount = 500;
    auto               fund_result = tm_->TransferFunds( kFundAmount, child_account_->GetAddress(), kTestTokenId );
    ASSERT_TRUE( fund_result.has_value() );

    ASSERT_WAIT_FOR_CONDITION( ([this, &kFundAmount]() { return account_->GetUTXOManager().GetBalance( kTestTokenId, child_account_->GetAddress() ) >= kFundAmount; }),
        std::chrono::milliseconds( 10000 ),
        "Child's funded balance should be visible within timeout",
        nullptr );

    std::vector<InputUTXOInfo> inputs;
    for ( const auto &utxo : account_->GetUTXOManager().GetUnconsumedUTXOs( child_account_->GetAddress() ) )
    {
        if ( !( utxo.GetTokenID() == kTestTokenId ) )
        {
            continue;
        }
        InputUTXOInfo input;
        input.txid_hash_  = utxo.GetTxID();
        input.output_idx_ = utxo.GetOutputIdx();
        input.signature_  = child_account_->Sign( input.SerializeForSigning() ); // the CHILD's own key
        inputs.push_back( std::move( input ) );
    }
    ASSERT_FALSE( inputs.empty() ) << "Child should have at least one unconsumed UTXO after funding";

    auto build_and_check = [&]( const std::string &dest_address )
    {
        SGTransaction::DAGStruct dag;
        dag.set_type( "transfer" );
        dag.set_source_addr( child_account_->GetAddress() );
        dag.set_nonce( 0 );

        std::vector<OutputDestInfo> outputs{ { 100, dest_address, kTestTokenId } };

        auto tx = std::make_shared<TransferTransaction>( TransferTransaction::New( inputs, outputs, dag ) );
        tx->MakeSignature( *child_account_ ); // the CHILD's own key — genuine self-signed spend

        EXPECT_TRUE( tm_->CheckParentChildAuthority( *tx ) )
            << "A child-self-signed transfer must be approved unconditionally (no destination restriction)";
        EXPECT_TRUE( tm_->CheckTransactionAuthorization( *tx ) )
            << "A child-self-signed transfer's own-key signature must verify";
    };

    build_and_check( std::string( 128, 'y' ) ); // arbitrary destination
    build_and_check( account_->GetAddress() );  // back to its own registered main
}

// ---------------------------------------------------------------------------
// ChildTransferToDevWalletUnaffected — REGR-02: a child-signed transfer to a
// developer-style destination passes unchanged. Per GeniusNode::PayDev's
// verified implementation (GeniusNode.cpp:2331-2334 — a thin wrapper over
// TransferFunds with zero separate code path), any child-signed transfer to
// an arbitrary destination transitively proves REGR-02 without needing to
// instantiate a full GeniusNode in this fixture.
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, ChildTransferToDevWalletUnaffected )
{
    tm_->Start();

    ASSERT_WAIT_FOR_CONDITION(
        [this]() { return tm_->GetState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 60000 ),
        "TransactionManager did not reach READY",
        nullptr );

    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "regr02_test" );
    auto child_reg = CertifyChildRegistration( 1, metadata );
    ASSERT_TRUE( blockchain_->CheckCertificate( child_reg.GetHash() ) );

    MintMainFunds( 1000, "regr02_migration" );

    constexpr uint64_t kFundAmount = 500;
    auto               fund_result = tm_->TransferFunds( kFundAmount, child_account_->GetAddress(), kTestTokenId );
    ASSERT_TRUE( fund_result.has_value() );

    ASSERT_WAIT_FOR_CONDITION( ([this, &kFundAmount]() { return account_->GetUTXOManager().GetBalance( kTestTokenId, child_account_->GetAddress() ) >= kFundAmount; }),
        std::chrono::milliseconds( 10000 ),
        "Child's funded balance should be visible within timeout",
        nullptr );

    std::vector<InputUTXOInfo> inputs;
    for ( const auto &utxo : account_->GetUTXOManager().GetUnconsumedUTXOs( child_account_->GetAddress() ) )
    {
        if ( !( utxo.GetTokenID() == kTestTokenId ) )
        {
            continue;
        }
        InputUTXOInfo input;
        input.txid_hash_  = utxo.GetTxID();
        input.output_idx_ = utxo.GetOutputIdx();
        input.signature_  = child_account_->Sign( input.SerializeForSigning() );
        inputs.push_back( std::move( input ) );
    }
    ASSERT_FALSE( inputs.empty() ) << "Child should have at least one unconsumed UTXO after funding";

    std::string dev_wallet_address( 128, 'x' ); // developer-style destination

    SGTransaction::DAGStruct dag;
    dag.set_type( "transfer" );
    dag.set_source_addr( child_account_->GetAddress() );
    dag.set_nonce( 0 );

    std::vector<OutputDestInfo> outputs{ { 100, dev_wallet_address, kTestTokenId } };

    auto tx = std::make_shared<TransferTransaction>( TransferTransaction::New( inputs, outputs, dag ) );
    tx->MakeSignature( *child_account_ );

    EXPECT_TRUE( tm_->CheckParentChildAuthority( *tx ) )
        << "A child-signed PayDev-style transfer must be approved unconditionally";
    EXPECT_TRUE( tm_->CheckTransactionAuthorization( *tx ) )
        << "A child-signed PayDev-style transfer's own-key signature must verify";
}

// ---------------------------------------------------------------------------
// ChildCannotClaimMainAsSourceRejected — REGR-03: a transaction claiming
// src=main_addr but signed with the child's own key is rejected by
// CheckTransactionAuthorization. The child never holds main's private key
// regardless of what src it claims — unaffected by every change in Plans
// 01-03, since the new certified-main branch only ever accepts a signature
// made with the CERTIFIED MAIN's own key, never a child's key masquerading
// as main.
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, ChildCannotClaimMainAsSourceRejected )
{
    SGTransaction::DAGStruct dag;
    dag.set_type( "transfer" );
    dag.set_source_addr( account_->GetAddress() ); // claiming to BE main
    dag.set_nonce( 0 );

    std::vector<InputUTXOInfo>  inputs;
    std::vector<OutputDestInfo> outputs{ { 100, std::string( 128, 'w' ), kTestTokenId } };

    auto tx = std::make_shared<TransferTransaction>( TransferTransaction::New( inputs, outputs, dag ) );
    tx->MakeSignature( *child_account_ ); // the child's key, which does not match account_'s address

    EXPECT_FALSE( tm_->CheckTransactionAuthorization( *tx ) )
        << "A child cannot spend main's UTXOs even claiming src=main_addr — the whole-tx signature "
           "check must fail since the child never holds main's private key";
}

// ===================================================================
// Phase 5 Plan 04 — Detach/Replace-Main adversarial + E2E tests
// (LIFE-01/02/03/04, gate 3b fork detection, nonce-chain replay).
// ===================================================================

// ---------------------------------------------------------------------------
// FilterRegistrationRejectsForkedSupersedesSequence — Gate 3b (D-38): a
// lifecycle-change RegistrationTx whose supersedes_sequence no longer matches
// the currently-stored sequence (a fork attempt) is rejected.
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, FilterRegistrationRejectsForkedSupersedesSequence )
{
    // Pre-populate the CRDT with a base registration at sequence=1.
    SGTransaction::DAGStruct dag1;
    dag1.set_type( "registration" );
    dag1.set_source_addr( account_->GetAddress() );
    dag1.set_nonce( 0 );

    SGTransaction::RegistrationMetadata metadata1;
    std::string                        main_address( 128, 'f' );

    auto base_tx = RegistrationTransaction::New( main_address, 1, metadata1, dag1 );
    base_tx.MakeSignature( *account_ );

    std::string            reg_key = TransactionManager::GetBlockChainBase() + "reg/" + account_->GetAddress();
    auto                    serialized_base = base_tx.SerializeByteVector();
    base::Buffer            base_buffer( std::vector<uint8_t>( serialized_base.begin(), serialized_base.end() ) );
    crdt::HierarchicalKey   hk( reg_key );
    auto                    put_result = db_->Put( hk, base_buffer, {} );
    ASSERT_TRUE( put_result.has_value() ) << "Pre-population Put should succeed";

    // First lifecycle-change element: sequence=2, detach_flag=true, supersedes_sequence=1
    // (matches the base's stored sequence) — should be accepted.
    SGTransaction::DAGStruct dag2;
    dag2.set_type( "registration" );
    dag2.set_source_addr( account_->GetAddress() );
    dag2.set_nonce( 0 );

    SGTransaction::RegistrationMetadata metadata2;
    std::string                        zero_address( 128, '0' );
    auto first_change_tx = RegistrationTransaction::New( zero_address, 2, metadata2, dag2,
                                                         /*detach_flag=*/true, /*supersedes_sequence=*/1 );
    first_change_tx.MakeSignature( *account_ );

    auto serialized_first = first_change_tx.SerializeByteVector();

    crdt::pb::Element first_element;
    first_element.set_key( "/bc/999/reg/" + account_->GetAddress() );
    first_element.set_value( std::string( serialized_first.begin(), serialized_first.end() ) );

    auto first_result = RegistrationE2ETestAccess::FilterRegistration( *tm_, first_element );
    EXPECT_FALSE( first_result.has_value() )
        << "First lifecycle-change element with a valid supersedes_sequence should be accepted";

    // FilterRegistration only VALIDATES — it does not itself write the CRDT record in this
    // direct-accessor-call test path. Re-Put the accepted element's own bytes at the same
    // reg/ key so the stored sequence genuinely advances to 2, mirroring how the existing
    // monotonic-sequence tests manually advance stored state between assertions.
    base::Buffer first_buffer( std::vector<uint8_t>( serialized_first.begin(), serialized_first.end() ) );
    auto         advance_result = db_->Put( hk, first_buffer, {} );
    ASSERT_TRUE( advance_result.has_value() ) << "Advancing stored state Put should succeed";

    // Second lifecycle-change element: sequence=3 (higher, so gate (d)'s monotonicity check
    // passes on its own), but supersedes_sequence=1 — now STALE, since the stored sequence has
    // moved to 2. This is the fork: a lifecycle-change tx claiming to supersede a sequence that
    // has itself already been superseded.
    SGTransaction::DAGStruct dag3;
    dag3.set_type( "registration" );
    dag3.set_source_addr( account_->GetAddress() );
    dag3.set_nonce( 0 );

    SGTransaction::RegistrationMetadata metadata3;
    auto forked_tx = RegistrationTransaction::New( zero_address, 3, metadata3, dag3,
                                                    /*detach_flag=*/true, /*supersedes_sequence=*/1 );
    forked_tx.MakeSignature( *account_ );

    auto serialized_forked = forked_tx.SerializeByteVector();

    crdt::pb::Element forked_element;
    forked_element.set_key( "/bc/999/reg/" + account_->GetAddress() );
    forked_element.set_value( std::string( serialized_forked.begin(), serialized_forked.end() ) );

    auto forked_result = RegistrationE2ETestAccess::FilterRegistration( *tm_, forked_element );
    EXPECT_TRUE( forked_result.has_value() )
        << "A lifecycle-change element with a stale supersedes_sequence (fork) should be tombstoned";
}

// ---------------------------------------------------------------------------
// FilterRegistrationRejectsMissingSupersedesLink — Gate 3b (D-38): a
// lifecycle-change RegistrationTx with no prior reg/ record at all is
// rejected (the "!current" fork-detection branch, design doc §9.3).
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, FilterRegistrationRejectsMissingSupersedesLink )
{
    // No reg/ record is pre-populated for account_'s address in this test.
    SGTransaction::DAGStruct dag;
    dag.set_type( "registration" );
    dag.set_source_addr( account_->GetAddress() );
    dag.set_nonce( 0 );

    SGTransaction::RegistrationMetadata metadata;
    std::string                        zero_address( 128, '0' );

    // Claims to supersede sequence=1, but no reg/ record exists at all for this address.
    auto orphan_tx = RegistrationTransaction::New( zero_address, 1, metadata, dag,
                                                    /*detach_flag=*/true, /*supersedes_sequence=*/1 );
    orphan_tx.MakeSignature( *account_ );

    auto serialized = orphan_tx.SerializeByteVector();

    crdt::pb::Element element;
    element.set_key( "/bc/999/reg/" + account_->GetAddress() );
    element.set_value( std::string( serialized.begin(), serialized.end() ) );

    auto result = RegistrationE2ETestAccess::FilterRegistration( *tm_, element );

    EXPECT_TRUE( result.has_value() )
        << "A lifecycle-change RegistrationTx with no prior reg/ record should be tombstoned (fork detected)";
}

// ---------------------------------------------------------------------------
// DetachChildEndToEnd — TransactionManager::DetachChild (D-35): the stored
// reg/ record ends with detach_flag=true, main_address=128-char zero
// sentinel, sequence=2 (1 from initial registration, 2 from Detach).
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, DetachChildEndToEnd )
{
    tm_->Start();

    ASSERT_WAIT_FOR_CONDITION(
        [this]() { return tm_->GetState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 60000 ),
        "TransactionManager did not reach READY",
        nullptr );

    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "detach_e2e_test" );

    auto register_result = tm_->RegisterChild( std::string( 128, 'r' ), metadata, 1 );
    ASSERT_TRUE( register_result.has_value() ) << "Initial registration should succeed";

    std::string            reg_key = TransactionManager::GetBlockChainBase() + "reg/" + account_->GetAddress();
    crdt::HierarchicalKey  hk( reg_key );

    // Allow time for the initial registration's CRDT write to land before Detach's
    // auto-derive overload reads it back.
    ASSERT_WAIT_FOR_CONDITION( ([this, &hk]() { auto get_result = db_->Get( hk ); return get_result.has_value(); }),
        std::chrono::milliseconds( 5000 ),
        "Initial registration CRDT write should be visible before Detach",
        nullptr );

    auto detach_result = tm_->DetachChild( metadata );
    ASSERT_TRUE( detach_result.has_value() ) << "DetachChild should succeed against an existing registration";

    std::shared_ptr<RegistrationTransaction> stored_reg;
    ASSERT_WAIT_FOR_CONDITION( ([this, &stored_reg, &hk]() {
            auto get_result = db_->Get( hk );
            if ( get_result.has_value() )
            {
                auto deserialize_result = TransactionManager::DeSerializeTransaction( get_result.value() );
                if ( !deserialize_result.has_error() )
                {
                    auto candidate = std::dynamic_pointer_cast<RegistrationTransaction>( deserialize_result.value() );
                    if ( candidate && candidate->GetDetachFlag() )
                    {
                        stored_reg = candidate;
                        return true;
                    }
                }
            }
            return false;
        }),
        std::chrono::milliseconds( 10000 ),
        "Detached reg/ record should be visible within timeout",
        nullptr );

    ASSERT_NE( stored_reg, nullptr ) << "Detached reg/ record should be visible within timeout";
    EXPECT_EQ( stored_reg->GetMainAddress(), std::string( 128, '0' ) );
    EXPECT_EQ( stored_reg->GetSequence(), 2 );
    EXPECT_TRUE( stored_reg->GetDetachFlag() );
}

// ---------------------------------------------------------------------------
// ReplaceMainEndToEnd — TransactionManager::ReplaceMain (D-37): the stored
// reg/ record ends with the new main_address and detach_flag=false.
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, ReplaceMainEndToEnd )
{
    tm_->Start();

    ASSERT_WAIT_FOR_CONDITION(
        [this]() { return tm_->GetState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 60000 ),
        "TransactionManager did not reach READY",
        nullptr );

    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "replace_main_e2e_test" );

    auto register_result = tm_->RegisterChild( std::string( 128, 's' ), metadata, 1 );
    ASSERT_TRUE( register_result.has_value() ) << "Initial registration should succeed";

    std::string            reg_key = TransactionManager::GetBlockChainBase() + "reg/" + account_->GetAddress();
    crdt::HierarchicalKey  hk( reg_key );

    ASSERT_WAIT_FOR_CONDITION( ([this, &hk]() { auto get_result = db_->Get( hk ); return get_result.has_value(); }),
        std::chrono::milliseconds( 5000 ),
        "Initial registration CRDT write should be visible before ReplaceMain",
        nullptr );

    std::string new_main_address( 128, 'u' );
    auto        replace_result = tm_->ReplaceMain( new_main_address, metadata );
    ASSERT_TRUE( replace_result.has_value() ) << "ReplaceMain should succeed against an existing registration";

    std::shared_ptr<RegistrationTransaction> stored_reg;
    ASSERT_WAIT_FOR_CONDITION( ([this, &stored_reg, &hk, &new_main_address]() {
            auto get_result = db_->Get( hk );
            if ( get_result.has_value() )
            {
                auto deserialize_result = TransactionManager::DeSerializeTransaction( get_result.value() );
                if ( !deserialize_result.has_error() )
                {
                    auto candidate = std::dynamic_pointer_cast<RegistrationTransaction>( deserialize_result.value() );
                    if ( candidate && candidate->GetMainAddress() == new_main_address )
                    {
                        stored_reg = candidate;
                        return true;
                    }
                }
            }
            return false;
        }),
        std::chrono::milliseconds( 10000 ),
        "Replace-Main'd reg/ record should be visible within timeout",
        nullptr );

    ASSERT_NE( stored_reg, nullptr ) << "Replace-Main'd reg/ record should be visible within timeout";
    EXPECT_EQ( stored_reg->GetMainAddress(), new_main_address );
    EXPECT_FALSE( stored_reg->GetDetachFlag() );
    EXPECT_EQ( stored_reg->GetSequence(), 2 );
}

// ---------------------------------------------------------------------------
// ReRegistrationAfterDetachViaReplaceMain — D-39: a child can re-register at
// any time after Detach via ReplaceMain, proven end-to-end through the real
// gate 3b/sequence-monotonicity checks.
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, ReRegistrationAfterDetachViaReplaceMain )
{
    tm_->Start();

    ASSERT_WAIT_FOR_CONDITION(
        [this]() { return tm_->GetState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 60000 ),
        "TransactionManager did not reach READY",
        nullptr );

    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "reregister_e2e_test" );

    auto register_result = tm_->RegisterChild( std::string( 128, 'v' ), metadata, 1 );
    ASSERT_TRUE( register_result.has_value() ) << "Initial registration should succeed";

    std::string            reg_key = TransactionManager::GetBlockChainBase() + "reg/" + account_->GetAddress();
    crdt::HierarchicalKey  hk( reg_key );

ASSERT_WAIT_FOR_CONDITION( ([this, &hk]() { auto get_result = db_->Get( hk ); return get_result.has_value(); }),
        std::chrono::milliseconds( 5000 ),
        "Initial registration CRDT write should be visible before Detach",
        nullptr );

    auto detach_result = tm_->DetachChild( metadata );
    ASSERT_TRUE( detach_result.has_value() ) << "DetachChild should succeed against an existing registration";

    // Poll until the Detach's write is visible before attempting re-registration.
    ASSERT_WAIT_FOR_CONDITION( ([this, &hk]() {
            auto get_result = db_->Get( hk );
            if ( get_result.has_value() )
            {
                auto deserialize_result = TransactionManager::DeSerializeTransaction( get_result.value() );
                if ( !deserialize_result.has_error() )
                {
                    auto candidate = std::dynamic_pointer_cast<RegistrationTransaction>( deserialize_result.value() );
                    if ( candidate && candidate->GetDetachFlag() )
                        return true;
                }
            }
            return false;
        }),
        std::chrono::milliseconds( 10000 ),
        "Detach should be visible before re-registration is attempted",
        nullptr );

    std::string third_main_address( 128, 'w' );
    auto        reregister_result = tm_->ReplaceMain( third_main_address, metadata );
    ASSERT_TRUE( reregister_result.has_value() )
        << "ReplaceMain should succeed as a re-registration after a prior Detach (D-39)";

    std::shared_ptr<RegistrationTransaction> stored_reg;
    ASSERT_WAIT_FOR_CONDITION( ([this, &stored_reg, &hk, &third_main_address]() {
            auto get_result = db_->Get( hk );
            if ( get_result.has_value() )
            {
                auto deserialize_result = TransactionManager::DeSerializeTransaction( get_result.value() );
                if ( !deserialize_result.has_error() )
                {
                    auto candidate = std::dynamic_pointer_cast<RegistrationTransaction>( deserialize_result.value() );
                    if ( candidate && candidate->GetMainAddress() == third_main_address )
                    {
                        stored_reg = candidate;
                        return true;
                    }
                }
            }
            return false;
        }),
        std::chrono::milliseconds( 10000 ),
        "Re-registered reg/ record should be visible within timeout",
        nullptr );

    ASSERT_NE( stored_reg, nullptr ) << "Re-registered reg/ record should be visible within timeout";
    EXPECT_EQ( stored_reg->GetMainAddress(), third_main_address );
    EXPECT_FALSE( stored_reg->GetDetachFlag() );
    EXPECT_EQ( stored_reg->GetSequence(), 3 );
}

// ---------------------------------------------------------------------------
// DetachPreservesChildUTXOsKeypairNonce — LIFE-04: Detach leaves the child's
// address, UTXOs, and standalone transacting capability unaffected.
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, DetachPreservesChildUTXOsKeypairNonce )
{
    tm_->Start();

    ASSERT_WAIT_FOR_CONDITION(
        [this]() { return tm_->GetState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 60000 ),
        "TransactionManager did not reach READY",
        nullptr );

    MintMainFunds( 1000, "life04_migration" );

    const std::string address_before = account_->GetAddress();
    const uint64_t    balance_before = account_->GetUTXOManager().GetBalance( kTestTokenId, account_->GetAddress() );
    ASSERT_GT( balance_before, 0ULL ) << "Account should have a non-zero, checkable balance before Detach";

    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "life04_test" );

    auto register_result = tm_->RegisterChild( std::string( 128, 'l' ), metadata, 1 );
    ASSERT_TRUE( register_result.has_value() ) << "Initial registration should succeed";

    std::string            reg_key = TransactionManager::GetBlockChainBase() + "reg/" + account_->GetAddress();
    crdt::HierarchicalKey  hk( reg_key );

    ASSERT_WAIT_FOR_CONDITION( ([this, &hk]() { auto get_result = db_->Get( hk ); return get_result.has_value(); }),
        std::chrono::milliseconds( 5000 ),
        "Initial registration CRDT write should be visible before Detach",
        nullptr );

    auto detach_result = tm_->DetachChild( metadata );
    ASSERT_TRUE( detach_result.has_value() ) << "DetachChild should succeed against an existing registration";

    ASSERT_WAIT_FOR_CONDITION( ([this, &hk]() {
            auto get_result = db_->Get( hk );
            if ( get_result.has_value() )
            {
                auto deserialize_result = TransactionManager::DeSerializeTransaction( get_result.value() );
                if ( !deserialize_result.has_error() )
                {
                    auto candidate = std::dynamic_pointer_cast<RegistrationTransaction>( deserialize_result.value() );
                    if ( candidate && candidate->GetDetachFlag() )
                        return true;
                }
            }
            return false;
        }),
        std::chrono::milliseconds( 10000 ),
        "Detach should be visible within timeout",
        nullptr );

    // Address is unchanged — no keypair rotation API exists, confirmed by construction.
    EXPECT_EQ( account_->GetAddress(), address_before );

    // UTXO balance is unchanged across Detach — a lifecycle-change registration tx never
    // touches UTXOs.
    EXPECT_EQ( account_->GetUTXOManager().GetBalance( kTestTokenId, account_->GetAddress() ), balance_before );

    // The detached child remains a fully functional, standalone-capable wallet (D-39): it can
    // still submit an ordinary transfer after Detach.
    constexpr uint64_t kSmallAmount    = 50;
    auto               transfer_result = tm_->TransferFunds( kSmallAmount, std::string( 128, 'q' ), kTestTokenId );
    EXPECT_TRUE( transfer_result.has_value() )
        << "A detached child should still be able to submit an ordinary transfer";
}

// ---------------------------------------------------------------------------
// LifecycleChangeReplayRejectedByNonceChain — design doc §9.4/T-03-12: a
// stale lifecycle-change RegistrationTx reusing an already-consumed nonce is
// rejected by the nonce chain, independent of and in addition to
// FilterRegistration's own sequence-monotonicity gate.
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, LifecycleChangeReplayRejectedByNonceChain )
{
    tm_->Start();

    ASSERT_WAIT_FOR_CONDITION(
        [this]() { return tm_->GetState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 60000 ),
        "TransactionManager did not reach READY",
        nullptr );

    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "replay_nonce_test" );

    // Consumes nonce 0 — this is the transaction whose nonce the replayed tx below will reuse.
    auto        register_result = tm_->RegisterChild( std::string( 128, 'n' ), metadata, 1 );
    ASSERT_TRUE( register_result.has_value() ) << "Initial registration should succeed";
    std::string register_hash = register_result.value();

    // Poll for CONFIRMED — CheckTransactionReplayProtection's nonce-chain check reads
    // account_m->GetPeerNonce(), which is only populated once a transaction is genuinely
    // CONFIRMED (certified), not merely SENDING.
    ASSERT_WAIT_FOR_CONDITION( ([this, &register_hash]() { return tm_->GetTransactionStatusByTxId( register_hash ) == TransactionManager::TransactionStatus::CONFIRMED; }),
        std::chrono::milliseconds( 30000 ),
        "Initial registration should be CONFIRMED within timeout",
        nullptr );

    // Consumes nonce 1 — advances the nonce chain further, matching the plan's setup sequence.
    auto detach_result = tm_->DetachChild( metadata );
    ASSERT_TRUE( detach_result.has_value() ) << "DetachChild should succeed against an existing registration";
    EXPECT_WAIT_FOR_CONDITION( ([this, &detach_result]() {
            auto s = tm_->GetTransactionStatusByTxId( detach_result.value() );
            return s == TransactionManager::TransactionStatus::SENDING ||
                   s == TransactionManager::TransactionStatus::CONFIRMED;
        }),
        std::chrono::milliseconds( 10000 ),
        "DetachChild should reach SENDING or CONFIRMED status",
        nullptr );

    // Manually construct a "replayed" lifecycle-change RegistrationTransaction reusing nonce 0
    // (the STALE, already-consumed nonce) — everything else about this tx is well-formed and
    // would otherwise be a plausible Detach.
    SGTransaction::DAGStruct dag;
    dag.set_type( "registration" );
    dag.set_source_addr( account_->GetAddress() );
    dag.set_nonce( 0 );
    dag.set_timestamp( std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch() )
                            .count() );

    SGTransaction::RegistrationMetadata replay_metadata;
    auto stale_tx = RegistrationTransaction::New( std::string( 128, '0' ), 2, replay_metadata, dag,
                                                  /*detach_flag=*/true, /*supersedes_sequence=*/1 );
    stale_tx.MakeSignature( *account_ );

    EXPECT_FALSE( tm_->CheckTransactionReplayProtection( stale_tx ) )
        << "A lifecycle-change RegistrationTx reusing an already-consumed nonce must be rejected "
           "by the nonce chain, independent of FilterRegistration's own sequence-monotonicity gate";
}

// ===================================================================
// Phase 5 Plan 05 — Revoke adversarial + E2E test coverage (LIFE-01/02/04,
// the main-initiated half of the lifecycle surface's adversarial coverage;
// Plan 04 above covered the child-initiated Detach/Replace-Main half).
// ===================================================================

// ---------------------------------------------------------------------------
// RevokeChildEndToEnd — TransactionManager::RevokeChild (D-36): a certified
// child's registration is revoked by main; the resulting reg/ record ends
// with detach_flag=true while main_address/sequence are preserved.
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, RevokeChildEndToEnd )
{
    tm_->Start();

    ASSERT_WAIT_FOR_CONDITION(
        [this]() { return tm_->GetState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 60000 ),
        "TransactionManager did not reach READY",
        nullptr );

    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "revoke_e2e_test" );
    auto child_reg = CertifyChildRegistration( 1, metadata );
    ASSERT_TRUE( blockchain_->CheckCertificate( child_reg.GetHash() ) )
        << "Child registration must be certified before Revoke can be attempted";

    auto revoke_result = tm_->RevokeChild( child_account_->GetAddress() );
    ASSERT_TRUE( revoke_result.has_value() ) << "RevokeChild should succeed against a certified child";

    std::string           reg_key = TransactionManager::GetBlockChainBase() + "reg/" + child_account_->GetAddress();
    crdt::HierarchicalKey hk( reg_key );

    std::shared_ptr<RegistrationTransaction> stored_reg;
    ASSERT_WAIT_FOR_CONDITION( ([this, &stored_reg, &hk]() {
            auto get_result = db_->Get( hk );
            if ( get_result.has_value() )
            {
                auto deserialize_result = TransactionManager::DeSerializeTransaction( get_result.value() );
                if ( !deserialize_result.has_error() )
                {
                    auto candidate = std::dynamic_pointer_cast<RegistrationTransaction>( deserialize_result.value() );
                    if ( candidate && candidate->GetDetachFlag() )
                    {
                        stored_reg = candidate;
                        return true;
                    }
                }
            }
            return false;
        }),
        std::chrono::milliseconds( 10000 ),
        "Revoked reg/ record should be visible within timeout",
        nullptr );

    ASSERT_NE( stored_reg, nullptr ) << "Revoked reg/ record should be visible within timeout";
    EXPECT_EQ( stored_reg->GetMainAddress(), account_->GetAddress() );
    EXPECT_EQ( stored_reg->GetSequence(), 1 );
    EXPECT_TRUE( stored_reg->GetDetachFlag() );
}

// ---------------------------------------------------------------------------
// RevokeRejectedForNonMain — T-05-09: a RevokeTx signed by an address that is
// NOT the certified main on record is rejected by CheckParentChildAuthority,
// while ordinary signature verification (CheckTransactionAuthorization) still
// passes — isolating the rejection to the parent-child authority gate.
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, RevokeRejectedForNonMain )
{
    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "revoke_nonmain_test" );
    auto child_reg = CertifyChildRegistration( 1, metadata );
    ASSERT_TRUE( blockchain_->CheckCertificate( child_reg.GetHash() ) )
        << "Child registration must be certified for the non-main rejection check to even apply";

    // A third, unrelated identity — neither the certified main nor the child.
    auto attacker = GeniusAccount::New( kTestTokenId, base_path / "revoke_attacker" );
    ASSERT_NE( attacker, nullptr );

    SGTransaction::DAGStruct dag;
    dag.set_type( "revoke" );
    dag.set_source_addr( attacker->GetAddress() ); // NOT the certified main's address
    dag.set_nonce( 0 );

    auto tx = std::make_shared<RevokeTransaction>( RevokeTransaction::New( child_account_->GetAddress(), 1, dag ) );
    tx->MakeSignature( *attacker ); // attacker validly signs as themselves — signature check passes

    EXPECT_FALSE( tm_->CheckParentChildAuthority( *tx ) )
        << "A RevokeTx signed by a non-main address must be rejected by CheckParentChildAuthority";
    EXPECT_TRUE( tm_->CheckTransactionAuthorization( *tx ) )
        << "The rejection above must be the parent-child authority gate, not a signature failure";
}

// ---------------------------------------------------------------------------
// RevokeRejectedForAlreadyDetachedChild — a second RevokeTx targeting a reg/
// record that is already detach_flag==true is rejected by
// CheckParentChildAuthority.
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, RevokeRejectedForAlreadyDetachedChild )
{
    tm_->Start();

    ASSERT_WAIT_FOR_CONDITION(
        [this]() { return tm_->GetState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 60000 ),
        "TransactionManager did not reach READY",
        nullptr );

    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "revoke_already_detached_test" );
    auto child_reg = CertifyChildRegistration( 1, metadata );
    ASSERT_TRUE( blockchain_->CheckCertificate( child_reg.GetHash() ) )
        << "Child registration must be certified before Revoke can be attempted";

    auto revoke_result = tm_->RevokeChild( child_account_->GetAddress() );
    ASSERT_TRUE( revoke_result.has_value() ) << "First RevokeChild should succeed against a certified child";

    std::string           reg_key = TransactionManager::GetBlockChainBase() + "reg/" + child_account_->GetAddress();
    crdt::HierarchicalKey hk( reg_key );

    bool     detached        = false;
    uint64_t stored_sequence = 0;
    ASSERT_WAIT_FOR_CONDITION( ([this, &detached, &stored_sequence, &hk]() {
            auto get_result = db_->Get( hk );
            if ( get_result.has_value() )
            {
                auto deserialize_result = TransactionManager::DeSerializeTransaction( get_result.value() );
                if ( !deserialize_result.has_error() )
                {
                    auto candidate = std::dynamic_pointer_cast<RegistrationTransaction>( deserialize_result.value() );
                    if ( candidate && candidate->GetDetachFlag() )
                    {
                        detached        = true;
                        stored_sequence = candidate->GetSequence();
                        return true;
                    }
                }
            }
            return false;
        }),
        std::chrono::milliseconds( 10000 ),
        "First Revoke should be visible before the second Revoke is attempted",
        nullptr );
    ASSERT_TRUE( detached ) << "First Revoke should be visible before the second Revoke is attempted";

    // A SECOND RevokeTransaction targeting the SAME (now-detached) child, matching the
    // now-stored sequence, signed by the real (formerly-certified) main.
    SGTransaction::DAGStruct dag;
    dag.set_type( "revoke" );
    dag.set_source_addr( account_->GetAddress() );
    dag.set_nonce( 0 );

    auto second_tx = std::make_shared<RevokeTransaction>(
        RevokeTransaction::New( child_account_->GetAddress(), stored_sequence, dag ) );
    second_tx->MakeSignature( *account_ );

    EXPECT_FALSE( tm_->CheckParentChildAuthority( *second_tx ) )
        << "A second RevokeTx against an already-detached/revoked child must be rejected by "
           "CheckParentChildAuthority";
}

// ---------------------------------------------------------------------------
// RevokeRejectedForSequenceMismatch — a RevokeTx whose registration_sequence
// does not match the currently-stored reg/ record's sequence is rejected by
// CheckParentChildAuthority specifically, not by CheckTransactionAuthorization.
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, RevokeRejectedForSequenceMismatch )
{
    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "revoke_seq_mismatch_test" );
    auto child_reg = CertifyChildRegistration( 1, metadata );
    ASSERT_TRUE( blockchain_->CheckCertificate( child_reg.GetHash() ) )
        << "Child registration must be certified for the sequence-mismatch rejection check to even apply";

    SGTransaction::DAGStruct dag;
    dag.set_type( "revoke" );
    dag.set_source_addr( account_->GetAddress() );
    dag.set_nonce( 0 );

    // Deliberately wrong registration_sequence (certified at 1, this claims 99).
    auto tx = std::make_shared<RevokeTransaction>( RevokeTransaction::New( child_account_->GetAddress(), 99, dag ) );
    tx->MakeSignature( *account_ );

    EXPECT_FALSE( tm_->CheckParentChildAuthority( *tx ) )
        << "A RevokeTx with a mismatched registration_sequence must be rejected by CheckParentChildAuthority";
    EXPECT_TRUE( tm_->CheckTransactionAuthorization( *tx ) )
        << "The rejection above must be the sequence-mismatch check, not a signature failure";
}

// ---------------------------------------------------------------------------
// ReRegistrationAfterRevoke — D-39: a revoked child can re-register (to the
// same or a different main) at a higher sequence. Re-registration is
// CHILD-initiated (ReplaceMain/DetachChild are always child-signed, D-37);
// since this fixture's child_account_ has no own TransactionManager (D-65),
// re-registration is proven the same way CertifyChildRegistration proves
// child-signed registration: manually build, child-sign, and certify a new
// RegistrationTransaction via the shared CertifySignedRegistrationTx helper.
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, ReRegistrationAfterRevoke )
{
    tm_->Start();

    ASSERT_WAIT_FOR_CONDITION(
        [this]() { return tm_->GetState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 60000 ),
        "TransactionManager did not reach READY",
        nullptr );

    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "rereg_after_revoke_test" );
    auto child_reg = CertifyChildRegistration( 1, metadata );
    ASSERT_TRUE( blockchain_->CheckCertificate( child_reg.GetHash() ) )
        << "Child registration must be certified before Revoke can be attempted";

    auto revoke_result = tm_->RevokeChild( child_account_->GetAddress() );
    ASSERT_TRUE( revoke_result.has_value() ) << "RevokeChild should succeed against a certified child";

    std::string           reg_key = TransactionManager::GetBlockChainBase() + "reg/" + child_account_->GetAddress();
    crdt::HierarchicalKey hk( reg_key );

    {
        ASSERT_WAIT_FOR_CONDITION( ([this, &hk]() {
                auto get_result = db_->Get( hk );
                if ( get_result.has_value() )
                {
                    auto deserialize_result = TransactionManager::DeSerializeTransaction( get_result.value() );
                    if ( !deserialize_result.has_error() )
                    {
                        auto candidate = std::dynamic_pointer_cast<RegistrationTransaction>( deserialize_result.value() );
                        if ( candidate && candidate->GetDetachFlag() )
                            return true;
                    }
                }
                return false;
            }),
            std::chrono::milliseconds( 10000 ),
            "Revoke should be visible before re-registration is attempted",
            nullptr );
    }

    std::string new_main_address( 128, 'p' );

    SGTransaction::DAGStruct dag;
    dag.set_type( "registration" );
    dag.set_source_addr( child_account_->GetAddress() );
    dag.set_nonce( 1 );
    // Nonce > 0 requires previous_hash to resolve to a certified transaction for this same
    // address (EvaluateTransactionReplayProtection, TransactionManager.cpp:4648-4659) — child_reg
    // is child_account_'s nonce=0 certified tx, so it's the correct link for this nonce=1 tx.
    dag.set_previous_hash( child_reg.GetHash() );
    dag.set_timestamp( std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch() )
                            .count() );

    SGTransaction::RegistrationMetadata rereg_metadata;
    rereg_metadata.set_game_id( "rereg_after_revoke_test" );

    auto rereg_tx = RegistrationTransaction::New( new_main_address, /*sequence=*/2, rereg_metadata, dag,
                                                  /*detach_flag=*/false, /*supersedes_sequence=*/1 );
    rereg_tx.MakeSignature( *child_account_ );

    CertifySignedRegistrationTx( rereg_tx, *child_account_ );
    ASSERT_TRUE( blockchain_->CheckCertificate( rereg_tx.GetHash() ) )
        << "Re-registration should be certified before checking the stored reg/ record";

    std::shared_ptr<RegistrationTransaction> stored_reg;
    ASSERT_WAIT_FOR_CONDITION( ([this, &stored_reg, &hk, &new_main_address]() {
            auto get_result = db_->Get( hk );
            if ( get_result.has_value() )
            {
                auto deserialize_result = TransactionManager::DeSerializeTransaction( get_result.value() );
                if ( !deserialize_result.has_error() )
                {
                    auto candidate = std::dynamic_pointer_cast<RegistrationTransaction>( deserialize_result.value() );
                    if ( candidate && candidate->GetMainAddress() == new_main_address )
                    {
                        stored_reg = candidate;
                        return true;
                    }
                }
            }
            return false;
        }),
        std::chrono::milliseconds( 10000 ),
        "Re-registered reg/ record should be visible within timeout",
        nullptr );

    ASSERT_NE( stored_reg, nullptr ) << "Re-registered reg/ record should be visible within timeout";
    EXPECT_EQ( stored_reg->GetMainAddress(), new_main_address );
    EXPECT_FALSE( stored_reg->GetDetachFlag() );
    EXPECT_EQ( stored_reg->GetSequence(), 2 );
}

// ---------------------------------------------------------------------------
// RevokePreservesChildUTXOsKeypairNonce — LIFE-04: Revoke leaves the child's
// address and UTXOs unaffected, the revoked child remains a fully functional,
// standalone-capable wallet, and the former main's delegated authority over
// it is gone.
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, RevokePreservesChildUTXOsKeypairNonce )
{
    tm_->Start();

    ASSERT_WAIT_FOR_CONDITION(
        [this]() { return tm_->GetState() == TransactionManager::State::READY; },
        std::chrono::milliseconds( 60000 ),
        "TransactionManager did not reach READY",
        nullptr );

    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "revoke_life04_test" );
    auto child_reg = CertifyChildRegistration( 1, metadata );
    ASSERT_TRUE( blockchain_->CheckCertificate( child_reg.GetHash() ) )
        << "Child registration must be certified before funding/Revoke";

    MintMainFunds( 1000, "revoke_life04_migration" );

    constexpr uint64_t kFundAmount = 500;
    auto               fund_result = tm_->TransferFunds( kFundAmount, child_account_->GetAddress(), kTestTokenId );
    ASSERT_TRUE( fund_result.has_value() ) << "Main should be able to fund the certified child";

    ASSERT_WAIT_FOR_CONDITION( ([this, &kFundAmount]() { return account_->GetUTXOManager().GetBalance( kTestTokenId, child_account_->GetAddress() ) >= kFundAmount; }),
        std::chrono::milliseconds( 10000 ),
        "Child's funded balance should be visible within timeout",
        nullptr );
    const std::string address_before = child_account_->GetAddress();
    const uint64_t    balance_before =
        account_->GetUTXOManager().GetBalance( kTestTokenId, child_account_->GetAddress() );
    ASSERT_GE( balance_before, kFundAmount ) << "Child's funded balance should be visible within timeout";

    auto revoke_result = tm_->RevokeChild( child_account_->GetAddress() );
    ASSERT_TRUE( revoke_result.has_value() ) << "RevokeChild should succeed against a certified child";

    std::string           reg_key = TransactionManager::GetBlockChainBase() + "reg/" + child_account_->GetAddress();
    crdt::HierarchicalKey hk( reg_key );

    ASSERT_WAIT_FOR_CONDITION( ([this, &hk]() {
            auto get_result = db_->Get( hk );
            if ( get_result.has_value() )
            {
                auto deserialize_result = TransactionManager::DeSerializeTransaction( get_result.value() );
                if ( !deserialize_result.has_error() )
                {
                    auto candidate = std::dynamic_pointer_cast<RegistrationTransaction>( deserialize_result.value() );
                    if ( candidate && candidate->GetDetachFlag() )
                        return true;
                }
            }
            return false;
        }),
        std::chrono::milliseconds( 10000 ),
        "Revoke should be visible within timeout",
        nullptr );

    // Address is unchanged — no keypair rotation API exists, confirmed by construction.
    EXPECT_EQ( child_account_->GetAddress(), address_before );

    // UTXO balance is unchanged across Revoke — RevokeTx never touches UTXOs.
    EXPECT_EQ( account_->GetUTXOManager().GetBalance( kTestTokenId, child_account_->GetAddress() ), balance_before );

    // The revoked child remains a fully functional, standalone-capable wallet (D-39): a
    // child-self-signed transfer still passes both gates (mirroring
    // ChildTransferToArbitraryAndMainUnaffected's exact construction).
    std::vector<InputUTXOInfo> inputs;
    for ( const auto &utxo : account_->GetUTXOManager().GetUnconsumedUTXOs( child_account_->GetAddress() ) )
    {
        if ( !( utxo.GetTokenID() == kTestTokenId ) )
        {
            continue;
        }
        InputUTXOInfo input;
        input.txid_hash_  = utxo.GetTxID();
        input.output_idx_ = utxo.GetOutputIdx();
        input.signature_  = child_account_->Sign( input.SerializeForSigning() );
        inputs.push_back( std::move( input ) );
    }
    ASSERT_FALSE( inputs.empty() ) << "Revoked child should still have at least one unconsumed UTXO";

    SGTransaction::DAGStruct transfer_dag;
    transfer_dag.set_type( "transfer" );
    transfer_dag.set_source_addr( child_account_->GetAddress() );
    transfer_dag.set_nonce( 0 );

    std::vector<OutputDestInfo> outputs{ { 100, std::string( 128, 'y' ), kTestTokenId } };

    auto self_tx = std::make_shared<TransferTransaction>( TransferTransaction::New( inputs, outputs, transfer_dag ) );
    self_tx->MakeSignature( *child_account_ ); // the CHILD's own key — genuine self-signed spend

    EXPECT_TRUE( tm_->CheckParentChildAuthority( *self_tx ) )
        << "A revoked child's self-signed transfer must still be approved (revoke does not disable the "
           "child's own key)";
    EXPECT_TRUE( tm_->CheckTransactionAuthorization( *self_tx ) )
        << "A revoked child's own-key signature must still verify";

    // Negative invariant: the former main no longer has any delegated recovery authority over
    // the revoked child. ParseRevokeTransaction (TransactionManager.cpp, Plan 02) rewrites the
    // reg/ record's dag_struct from scratch when applying a Revoke — the rewritten record's own
    // data_hash was never itself submitted as a nonce-consensus subject, so
    // blockchain_->CheckCertifiedParent can no longer resolve a certificate for it. Confirm this
    // explicitly first (this is the mechanism, not detach_flag directly, that gates post-revoke
    // authority — CheckCertifiedParent's own CheckCertificate gate requires the CURRENT
    // certified registration to be read):
    auto post_revoke_certified_main = blockchain_->CheckCertifiedParent( child_account_->GetAddress() );
    EXPECT_TRUE( !post_revoke_certified_main.has_value() || *post_revoke_certified_main != account_->GetAddress() )
        << "CheckCertifiedParent should no longer resolve the revoked child to its former main";

    // A manually-built, main-signed, delegated-recovery-shaped transaction (mirroring
    // MainRecoveryWrongDestinationRejected's construction) targeting the revoked child. The
    // authoritative rejection lives in CheckTransactionAuthorization: its D-60 certified-main
    // delegation branch requires CheckCertifiedParent to resolve, which it no longer does once
    // revoked — so a main-signed, child-sourced transaction is no longer accepted as a valid
    // action at all (neither a genuine child-self-signed spend nor a still-certified delegated
    // recovery).
    SGTransaction::DAGStruct delegated_dag;
    delegated_dag.set_type( "transfer" );
    delegated_dag.set_source_addr( child_account_->GetAddress() );
    delegated_dag.set_nonce( 0 );

    std::vector<InputUTXOInfo>  delegated_inputs;
    std::vector<OutputDestInfo> delegated_outputs{ { 100, account_->GetAddress(), kTestTokenId } };

    auto delegated_tx = std::make_shared<TransferTransaction>(
        TransferTransaction::New( delegated_inputs, delegated_outputs, delegated_dag ) );
    delegated_tx->MakeSignature( *account_ ); // main signs — this is what makes it "delegated"

    EXPECT_FALSE( tm_->CheckTransactionAuthorization( *delegated_tx ) )
        << "A former main's delegated-recovery-shaped transaction against a revoked child must fail "
           "CheckTransactionAuthorization once CheckCertifiedParent no longer resolves the child's "
           "certified main";
}
