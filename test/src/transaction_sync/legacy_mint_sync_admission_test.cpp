/**
 * @file       legacy_mint_sync_admission_test.cpp
 * @brief      MINT-03 sync-admission gate for legacy-v1 mints (certificate-
 *             conditional defer, never a blanket type block).
 * @details    Adversarial + compat pairs (VER-01 discipline) for the third and
 *             last ingress: a value-carrying v1 mint (type "mint", amount > 0)
 *             arriving through CRDT sync is admitted only once its slot
 *             certificate exists locally (Stall until then, bounded by the
 *             D-03 defer window, deadline Reject on expiry), while zero-value
 *             migration fillers, certified history, mint-v2 and transfers keep
 *             today's verdicts. The stall-resolution negative control closes
 *             the VALIDATION.md Wave-0 gap: the defer must provably resolve.
 * @date       2026-10-20
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "account/GeniusAccount.hpp"
#include "account/MintTransaction.hpp"
#include "account/MintTransactionV2.hpp"
#include "account/TokenID.hpp"
#include "account/TransferTransaction.hpp"
#include "blockchain/Blockchain.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "base/hexutil.hpp"
#include "crdt/crdt_data_filter.hpp"
#include "crypto/hasher.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/wait_condition.hpp"
#include "transaction/TransactionManager.hpp"

using namespace sgns;

namespace sgns
{
    /**
     * @brief Friend accessor for the private sync-admission surface under test
     *        (FilterTransaction). Declared as a friend in TransactionManager.hpp.
     */
    class LegacyMintSyncAdmissionTestAccess
    {
    public:
        static crdt::CRDTDataFilter::ElementFilterResult FilterTransaction( TransactionManager      &tm,
                                                                            const crdt::pb::Element &element )
        {
            return tm.FilterTransaction( element );
        }
    };
} // namespace sgns

namespace
{
    /// @brief Test token identifier (same value the admission and bridge suites use).
    const sgns::TokenID kTestTokenId = sgns::TokenID::FromBytes( { 0x00 } );

    /// @brief Source chain id of the historical v1 mint (a real public chain id).
    constexpr const char *kSourceChainId = "11155111";

    using FilterDecision = crdt::CRDTDataFilter::ElementFilterResult::Decision;
} // namespace

/**
 * @brief Lightweight fixture using CRDTFixture for database/pubsub and creating a
 *        Blockchain + TransactionManager directly (no GeniusNode, no network
 *        sync) — the same shape as legacy_mint_admission_test.cpp. The
 *        authorized-full-node address is pinned to the fixture account so the
 *        blockchain's ValidatorRegistry accepts its genesis registry (the same
 *        pin multi_account_sync.cpp applies before StoreRegistry).
 */
class LegacyMintSyncAdmissionTest : public ::test::CRDTFixture
{
public:
    LegacyMintSyncAdmissionTest() : ::test::CRDTFixture( "legacy_mint_sync_admission_test" )
    {
    }

    void SetUp() override
    {
        GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                                                { return std::make_shared<MemorySecureStorage>( identifier ); } );

        account_ = GeniusAccount::New( kTestTokenId, base_path / "account" );
        assert( account_ != nullptr );

        auto load_result = account_->GetUTXOManager().LoadUTXOs( db_->GetDataStore() );
        assert( load_result.has_value() );

        Blockchain::SetAuthorizedFullNodeAddress( account_->GetAddress() );

        blockchain_ = Blockchain::New( db_, account_, pubs_, []( outcome::result<void> ) {} );
        assert( blockchain_ != nullptr );

        constexpr auto kTimestampTolerance = std::chrono::milliseconds( 300000 );
        constexpr auto kMutabilityWindow   = std::chrono::milliseconds( 600000 );

        tm_ = TransactionManager::New( db_,
                                       io_,
                                       account_,
                                       blockchain_,
                                       NodeType::Light,
                                       0, // subnet_id
                                       kTimestampTolerance,
                                       kMutabilityWindow );
        assert( tm_ != nullptr );
    }

    ~LegacyMintSyncAdmissionTest() override
    {
        // Join the ConsensusManager round-timer thread before the base fixture
        // releases the pubsub host/io_context underneath it (same teardown
        // invariant as legacy_mint_admission_test.cpp).
        if ( blockchain_ )
        {
            (void) blockchain_->Stop();
        }
        tm_.reset();
        blockchain_.reset();
        account_.reset();
    }

    /// @brief Builds a fully signed v1 mint attributed to the fixture account.
    std::shared_ptr<MintTransaction> MakeSignedV1Mint( uint64_t amount )
    {
        SGTransaction::DAGStruct dag;
        dag.set_source_addr( account_->GetAddress() );
        dag.set_nonce( 0 );
        dag.set_timestamp( NowMs() );
        auto mint = std::make_shared<MintTransaction>(
            MintTransaction::New( amount, kSourceChainId, kTestTokenId, dag ) );
        mint->MakeSignature( *account_ );
        return mint;
    }

    /// @brief Builds a signed mint-v2 (the authorized mint path).
    std::shared_ptr<MintTransactionV2> MakeSignedMintV2()
    {
        SGTransaction::DAGStruct dag;
        dag.set_type( "mint-v2" );
        dag.set_source_addr( account_->GetAddress() );
        dag.set_nonce( 0 );
        dag.set_timestamp( NowMs() );

        const auto burn_hash = base::Hash256::fromReadableString( std::string( 64, 'a' ) );
        assert( burn_hash.has_value() );
        auto mint_v2 = std::make_shared<MintTransactionV2>( MintTransactionV2::New( 42,
                                                                                    kSourceChainId,
                                                                                    kTestTokenId,
                                                                                    std::move( dag ),
                                                                                    { { burn_hash.value(), 0, {} } },
                                                                                    account_->GetAddress() ) );
        mint_v2->MakeSignature( *account_ );
        return mint_v2;
    }

    /// @brief Builds a signed bare transfer (empty UTXO sets).
    std::shared_ptr<TransferTransaction> MakeSignedTransfer()
    {
        SGTransaction::DAGStruct dag;
        dag.set_type( "transfer" );
        dag.set_source_addr( account_->GetAddress() );
        dag.set_nonce( 0 );
        dag.set_timestamp( NowMs() );
        auto transfer = std::make_shared<TransferTransaction>( TransferTransaction::New( {}, {}, std::move( dag ) ) );
        transfer->MakeSignature( *account_ );
        return transfer;
    }

    /// @brief Serializes @p tx into the CRDT element shape the tx filter sees on
    ///        a sync delta (production stores SerializeByteVector bytes).
    crdt::pb::Element MakeTxElement( const GeniusTransaction &tx )
    {
        const auto        bytes = tx.SerializeByteVector();
        crdt::pb::Element element;
        element.set_key( TransactionManager::GetTransactionPath( tx ) );
        element.set_value( bytes.data(), bytes.size() );
        return element;
    }

    /// @brief CID of the fixture's genesis registry (stored lazily, then cached).
    std::string GenesisRegistryCid()
    {
        if ( genesis_registry_cid_.empty() )
        {
            auto registry     = blockchain_->GetValidatorRegistry();
            auto store_result = registry->StoreGenesisRegistry( { account_->GetAddress() },
                                                                [this]( std::vector<uint8_t> payload )
                                                                { return account_->Sign( std::move( payload ) ); } );
            EXPECT_FALSE( store_result.has_error() );
            std::chrono::milliseconds waited( 0 );
            sgns::test::assertWaitForCondition(
                [&registry]()
                {
                    auto load = registry->LoadCurrentRegistry();
                    return load.has_value() && !registry->GetRegistryCid().empty();
                },
                std::chrono::milliseconds( 2000 ),
                "genesis registry stored",
                &waited );
            genesis_registry_cid_ = registry->GetRegistryCid();
        }
        return genesis_registry_cid_;
    }

    /**
     * @brief Builds a fully signed certificate whose nonce subject embeds @p tx
     *        itself and writes the durable canonical /cert/<slot> record —
     *        exactly the record shape SubmitCertificate writes post-Approve
     *        (consensus_pending_lifecycle_test.cpp MakeCertificateForRegistry
     *        precedent). GetCertificateBySlot(tx.GetSlotID()) finds and Approves
     *        it through the D-01 grandfather probe.
     */
    void WriteMatchingCertificateFor( const GeniusTransaction &tx )
    {
        const auto registry_cid = GenesisRegistryCid();
        ASSERT_FALSE( registry_cid.empty() );

        auto subject = ConsensusManager::CreateNonceSubject( tx.GetSrcAddress(),
                                                             tx.GetNonce(),
                                                             tx.GetHash(),
                                                             tx.SerializeToEmbeddedTransaction(),
                                                             std::nullopt,
                                                             std::nullopt );
        ASSERT_TRUE( subject.has_value() );

        const auto now_ms = NowMs();

        ConsensusManager::Proposal proposal;
        *proposal.mutable_subject() = subject.value();
        proposal.set_proposer_id( account_->GetAddress() );
        proposal.set_registry_cid( registry_cid );
        proposal.set_registry_epoch( 0 );
        proposal.set_timestamp( now_ms );
        // Replicates ConsensusManager::CreateProposalId (private static): hash
        // the signing bytes of the proposal with its id cleared.
        ConsensusManager::Proposal id_copy = proposal;
        id_copy.clear_proposal_id();
        auto id_signing = ConsensusManager::ProposalSigningBytes( id_copy );
        ASSERT_TRUE( id_signing.has_value() );
        auto id_hash = crypto::sha2_256( id_signing.value().data(), id_signing.value().size() );
        proposal.set_proposal_id( base::hex_lower( gsl::span<const uint8_t>( id_hash.data(), id_hash.size() ) ) );
        auto proposal_signing = ConsensusManager::ProposalSigningBytes( proposal );
        ASSERT_TRUE( proposal_signing.has_value() );
        auto proposal_signature = account_->Sign( proposal_signing.value() );
        proposal.set_signature( proposal_signature.data(), proposal_signature.size() );

        ConsensusManager::Vote vote;
        vote.set_proposal_id( proposal.proposal_id() );
        vote.set_voter_id( account_->GetAddress() );
        vote.set_approve( true );
        vote.set_timestamp( now_ms );
        auto vote_signing = ConsensusManager::VoteSigningBytes( vote );
        ASSERT_TRUE( vote_signing.has_value() );
        auto vote_signature = account_->Sign( vote_signing.value() );
        vote.set_signature( vote_signature.data(), vote_signature.size() );

        ConsensusManager::Certificate certificate;
        certificate.set_proposal_id( proposal.proposal_id() );
        certificate.set_registry_cid( registry_cid );
        certificate.set_registry_epoch( 0 );
        certificate.set_total_weight( 50000 );
        certificate.set_approved_weight( 50000 );
        certificate.set_timestamp( now_ms );
        *certificate.add_votes()        = vote;
        *certificate.mutable_proposal() = proposal;

        const auto durable_key = ConsensusManager::GetExpectedCertificateSlotKey( certificate );
        ASSERT_FALSE( durable_key.empty() );
        ASSERT_EQ( durable_key, std::string( "/cert/" ) + tx.GetSlotID() )
            << "The certificate must land exactly on the mint's canonical slot";

        std::string serialized;
        ASSERT_TRUE( certificate.SerializeToString( &serialized ) );
        crdt::GlobalDB::Buffer value;
        value.put( serialized );
        ASSERT_TRUE( db_->Put( { durable_key }, value, {} ).has_value() );
    }

    /// @brief Evaluates the sync-admission filter for @p tx as a sync delta would.
    crdt::CRDTDataFilter::ElementFilterResult Filter( const GeniusTransaction &tx )
    {
        return LegacyMintSyncAdmissionTestAccess::FilterTransaction( *tm_, MakeTxElement( tx ) );
    }

    static int64_t NowMs()
    {
        return static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );
    }

    std::shared_ptr<GeniusAccount>      account_;
    std::shared_ptr<Blockchain>         blockchain_;
    std::shared_ptr<TransactionManager> tm_;
    std::string                         genesis_registry_cid_;
};

// ---------------------------------------------------------------------------
// Adversarial: certificate-conditional defer (Wave-0 negative control)
// ---------------------------------------------------------------------------

// VER-01 adversarial case.
// Attacker authority: ANY peer on the gossip topic — a plain key pair able to
// self-sign a transaction and publish it into a CRDT delta. Since 21-05 blocks
// new v1 mints from ever obtaining a certificate (consensus ingress +
// submission preflight), "certificate exists locally" exactly separates
// historical certified v1 mints from new attacks.
// Expected: without the slot certificate the element stalls — stripped from
// the delta, unjournaled, re-evaluated on the retry schedule — never stored.
// Actual pre-fix behavior: FilterTransaction admitted any signature-valid
// transaction, storing the value-carrying v1 mint on first sight.
TEST_F( LegacyMintSyncAdmissionTest, ValueCarryingV1MintWithoutCertificateStalls )
{
    auto mint = MakeSignedV1Mint( 1 );

    const auto verdict = Filter( *mint );
    EXPECT_EQ( verdict.decision, FilterDecision::kStall )
        << "A value-carrying v1 mint without its slot certificate must defer, not admit";
}

// VER-01 negative control (VALIDATION.md Wave-0 gap): the defer must provably
// RESOLVE. The same element that stalled is re-evaluated after the certificate
// record lands (certificate-first delivery order is unordered across deltas),
// and must then be admitted — proving the stall is an ordering holdback, not a
// blanket type block that would strand historical sync.
TEST_F( LegacyMintSyncAdmissionTest, StalledV1MintResolvesOnceCertificateArrives )
{
    auto mint = MakeSignedV1Mint( 1 );

    const auto first = Filter( *mint );
    EXPECT_EQ( first.decision, FilterDecision::kStall );

    WriteMatchingCertificateFor( *mint );

    std::chrono::milliseconds waited( 0 );
    EXPECT_WAIT_FOR_CONDITION( [&]() { return Filter( *mint ).decision == FilterDecision::kAccept; },
                               std::chrono::milliseconds( 2000 ),
                               "stalled v1 mint resolves after certificate arrival",
                               &waited );
}

// ---------------------------------------------------------------------------
// Adversarial: D-03 deadline
// ---------------------------------------------------------------------------

// VER-01 adversarial case.
// Attacker authority: ANY peer flooding value-carrying v1 mints that can never
// present a certificate (their ingress is blocked by 21-05). The stall must be
// BOUNDED: after the (here test-shortened) defer window elapses, the element
// rejects through the existing should_delete/tombstone structure, carrying the
// proof-key tombstone so no orphaned proof element survives the rejection.
// Actual pre-fix behavior: the mint was admitted outright (no defer existed).
TEST_F( LegacyMintSyncAdmissionTest, DeferWindowExpiryRejectsWithProofKeyTombstone )
{
    tm_->SetLegacyMintDeferWindow( std::chrono::seconds( 1 ) );

    auto mint = MakeSignedV1Mint( 1 );

    const auto first = Filter( *mint );
    EXPECT_EQ( first.decision, FilterDecision::kStall );

    std::this_thread::sleep_for( std::chrono::milliseconds( 1100 ) );

    const auto second = Filter( *mint );
    ASSERT_EQ( second.decision, FilterDecision::kReject )
        << "An expired defer deadline must convert the stall into a rejection";
    ASSERT_FALSE( second.additional_elements_to_remove.empty() )
        << "Deadline rejection must carry the proof-key tombstone";

    const auto proof_key             = TransactionManager::GetTransactionProofPath( *mint );
    bool       tombstone_names_proof = false;
    for ( const auto &element : second.additional_elements_to_remove )
    {
        tombstone_names_proof = tombstone_names_proof || element.key() == proof_key;
    }
    EXPECT_TRUE( tombstone_names_proof )
        << "The tombstone must strip the associated proof key per the filter's existing structure";
}

// ---------------------------------------------------------------------------
// Compat: certified history, migration fillers, v2, transfers
// ---------------------------------------------------------------------------

// VER-01 compat twin of the defer case. A v1 mint that already has its matching
// FINALIZED certificate locally (historical certified v1 — pre-cutover history
// re-syncing to a fresh node) is admitted on the first pass: the probe hit is
// the proof this is history, not a new attack.
TEST_F( LegacyMintSyncAdmissionTest, CertifiedV1MintAdmittedFirstPass )
{
    auto mint = MakeSignedV1Mint( 1 );
    WriteMatchingCertificateFor( *mint );

    const auto verdict = Filter( *mint );
    EXPECT_EQ( verdict.decision, FilterDecision::kAccept )
        << "Certified historical v1 mints must keep syncing (D-02 history compat)";
}

// VER-01 compat twin. Zero-value v1 mints are the nonce fillers
// Migration3_4_0To3_5_0 synthesizes (MintTransaction::New(0, ...)); they never
// carry a certificate of their own and must traverse the sync filter unchanged.
TEST_F( LegacyMintSyncAdmissionTest, ZeroValueV1MintSyncsUnchanged )
{
    auto filler = MakeSignedV1Mint( 0 );

    const auto verdict = Filter( *filler );
    EXPECT_EQ( verdict.decision, FilterDecision::kAccept )
        << "Zero-amount v1 mints (migration nonce fillers) must never stall or reject";
}

// VER-01 compat twin for non-v1 traffic: a signed mint-v2 (the authorized mint
// path) and a signed transfer keep exactly the verdict they had before the
// gate — the branch classifies value-carrying v1 mints only.
TEST_F( LegacyMintSyncAdmissionTest, MintV2AndTransferKeepUnchangedSyncVerdicts )
{
    const auto mint_v2 = MakeSignedMintV2();
    EXPECT_FALSE( TransactionManager::IsValueCarryingLegacyMint( *mint_v2 ) );
    EXPECT_EQ( Filter( *mint_v2 ).decision, FilterDecision::kAccept )
        << "mint-v2 sync admission must be unchanged by the v1 gate";

    const auto transfer = MakeSignedTransfer();
    EXPECT_FALSE( TransactionManager::IsValueCarryingLegacyMint( *transfer ) );
    EXPECT_EQ( Filter( *transfer ).decision, FilterDecision::kAccept )
        << "transfer sync admission must be unchanged by the v1 gate";
}
