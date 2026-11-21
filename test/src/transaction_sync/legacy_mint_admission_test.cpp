/**
 * @file       legacy_mint_admission_test.cpp
 * @brief      MINT-03 admission gates for legacy-v1 mints at the synchronous ingresses.
 * @details    Adversarial + compat pairs (VER-01 discipline): a self-signed
 *             value-carrying v1 mint (type "mint", amount > 0) must be rejected
 *             at consensus ingress and at local/RPC submission preflight, while
 *             zero-amount migration fillers, mint-v2 and non-mint types keep
 *             today's verdicts. Deserialization stays open (D-02): the
 *             transaction_parsers map is untouched by design.
 * @date       2026-10-20
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <string>

#include "account/GeniusAccount.hpp"
#include "account/MintTransaction.hpp"
#include "account/MintTransactionV2.hpp"
#include "account/TokenID.hpp"
#include "account/TransferTransaction.hpp"
#include "blockchain/Blockchain.hpp"
#include "blockchain/Consensus.hpp"
#include "crypto/hasher.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "transaction/TransactionConsensusHandler.hpp"
#include "transaction/TransactionManager.hpp"
#include "testutil/storage/base_crdt_test.hpp"

using namespace sgns;

namespace sgns
{
    /**
     * @brief Friend accessor for the private admission surfaces under test.
     *        Declared as a friend in TransactionManager.hpp (SendTransactionItem,
     *        consensus_m_) and TransactionConsensusHandler.hpp
     *        (ValidateTransactionForConsensus, CheckTransactionTypeRules).
     */
    class LegacyMintAdmissionTestAccess
    {
    public:
        static ConsensusManager::ValidationResult ValidateForConsensus( TransactionManager      &tm,
                                                                        const GeniusTransaction &tx )
        {
            return tm.consensus_m_->ValidateTransactionForConsensus( tx );
        }

        static bool CheckTypeRules( TransactionManager &tm, const GeniusTransaction &tx )
        {
            return tm.consensus_m_->CheckTransactionTypeRules( tx );
        }

        static outcome::result<void> SendItem( TransactionManager &tm, TransactionManager::TransactionItem &item )
        {
            return tm.SendTransactionItem( item );
        }
    };
} // namespace sgns

namespace
{
    /// @brief Test token identifier (same value the bridge e2e suite uses).
    const sgns::TokenID kTestTokenId = sgns::TokenID::FromBytes( { 0x00 } );

    /// @brief Source chain id of the forged mint (a real public chain id).
    constexpr const char *kSourceChainId = "11155111";
} // namespace

/**
 * @brief Lightweight fixture using CRDTFixture for database/pubsub and creating a
 *        TransactionManager directly (no GeniusNode, no network sync) — the same
 *        shape as transaction_manager_certificate_fallback_test.cpp.
 */
class LegacyMintAdmissionTest : public ::test::CRDTFixture
{
public:
    LegacyMintAdmissionTest() : ::test::CRDTFixture( "legacy_mint_admission_test" )
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

    ~LegacyMintAdmissionTest() override
    {
        // Join the ConsensusManager round-timer thread before the base fixture
        // releases the pubsub host/io_context underneath it (same teardown
        // invariant as transaction_manager_certificate_fallback_test.cpp).
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

    /// @brief Builds an unfunded mint-v2 (the authorized mint path).
    std::shared_ptr<MintTransactionV2> MakeMintV2()
    {
        SGTransaction::DAGStruct dag;
        dag.set_type( "mint-v2" );
        dag.set_source_addr( account_->GetAddress() );
        dag.set_nonce( 0 );
        dag.set_timestamp( NowMs() );

        const auto burn_hash = base::Hash256::fromReadableString( std::string( 64, 'a' ) );
        assert( burn_hash.has_value() );
        return std::make_shared<MintTransactionV2>( MintTransactionV2::New( 42,
                                                                            kSourceChainId,
                                                                            kTestTokenId,
                                                                            std::move( dag ),
                                                                            { { burn_hash.value(), 0, {} } },
                                                                            account_->GetAddress() ) );
    }

    /// @brief Builds a bare transfer with no UTXO inputs/outputs.
    std::shared_ptr<TransferTransaction> MakeTransfer()
    {
        SGTransaction::DAGStruct dag;
        dag.set_type( "transfer" );
        dag.set_source_addr( account_->GetAddress() );
        dag.set_nonce( 0 );
        dag.set_timestamp( NowMs() );
        return std::make_shared<TransferTransaction>( TransferTransaction::New( {}, {}, std::move( dag ) ) );
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
};

// ---------------------------------------------------------------------------
// Adversarial: consensus ingress
// ---------------------------------------------------------------------------

// VER-01 adversarial case.
// Attacker authority: ANY account — a plain key pair able to self-sign a
// transaction; no validator, bridge relayer, or genesis authority is involved.
// Expected: the value-carrying v1 mint is rejected before it can be voted on.
// Actual pre-fix behavior: CheckTransactionTypeRules returned true for every
// transaction without UTXO parameters, so a self-signed v1 mint passed the full
// ValidateTransactionForConsensus chain with an Approve and could be certified.
TEST_F( LegacyMintAdmissionTest, SelfSignedValueCarryingV1MintRejectedAtConsensusIngress )
{
    auto mint = MakeSignedV1Mint( 1 );

    const auto verdict = LegacyMintAdmissionTestAccess::ValidateForConsensus( *tm_, *mint );
    EXPECT_EQ( verdict.check, ConsensusManager::Check::Reject );

    EXPECT_FALSE( LegacyMintAdmissionTestAccess::CheckTypeRules( *tm_, *mint ) )
        << "The type-rules verdict itself must flip to false for a value-carrying v1 mint";
}

// ---------------------------------------------------------------------------
// Adversarial: local/RPC submission preflight
// ---------------------------------------------------------------------------

// VER-01 adversarial case.
// Attacker authority: ANY account — a plain key pair able to self-sign a
// transaction and submit it locally or through the RPC submission path (which
// routes through the same SendTransactionItem preflight).
// Expected: the submission fails closed with a named error
// (std::errc::operation_not_permitted) BEFORE any CRDT Put/commit.
// Actual pre-fix behavior: the preflight loop only checked PubSub size and UTXO
// commitments, so the value-carrying v1 mint was published towards consensus.
TEST_F( LegacyMintAdmissionTest, SelfSignedValueCarryingV1MintPreflightFailsClosed )
{
    auto mint = MakeSignedV1Mint( 1 );

    TransactionManager::TransactionItem item;
    item.first.emplace_back( mint, std::nullopt );

    const auto result = LegacyMintAdmissionTestAccess::SendItem( *tm_, item );
    ASSERT_TRUE( result.has_error() ) << "A value-carrying v1 mint submission must fail";
    EXPECT_EQ( result.error(), std::errc::operation_not_permitted )
        << "The rejection must carry the named MINT-03 errc, not a generic failure";

    EXPECT_FALSE( tm_->KeyExistsInDB( TransactionManager::GetTransactionPath( *mint ) ) )
        << "The rejection must land before any CRDT Put of the rejected mint";
}

// VER-01 compat twin of the adversarial preflight case: zero-amount v1 mints
// and mint-v2 submissions take exactly the preflight path they take today —
// the gate may not introduce a NEW rejection for them. (An unfunded mint-v2
// still fails the pre-existing UTXO-commitment preflight with invalid_argument,
// which is precisely today's behavior.)
TEST_F( LegacyMintAdmissionTest, ZeroValueAndV2SubmissionsTakeTodaysPreflightPath )
{
    TransactionManager::TransactionItem filler_item;
    filler_item.first.emplace_back( MakeSignedV1Mint( 0 ), std::nullopt );
    const auto filler_result = LegacyMintAdmissionTestAccess::SendItem( *tm_, filler_item );
    ASSERT_TRUE( !filler_result.has_error() || filler_result.error() != std::errc::operation_not_permitted )
        << "Zero-amount v1 mint (migration filler) must not hit the MINT-03 rejection";

    TransactionManager::TransactionItem v2_item;
    v2_item.first.emplace_back( MakeMintV2(), std::nullopt );
    const auto v2_result = LegacyMintAdmissionTestAccess::SendItem( *tm_, v2_item );
    ASSERT_TRUE( !v2_result.has_error() || v2_result.error() != std::errc::operation_not_permitted )
        << "mint-v2 submission must not hit the MINT-03 rejection";
}

// ---------------------------------------------------------------------------
// Compat: zero-value migration fillers, mint-v2, non-mint types
// ---------------------------------------------------------------------------

// VER-01 compat twin of the adversarial ingress case.
// Zero-amount v1 mints are the nonce fillers Migration3_4_0To3_5_0 synthesizes
// (MintTransaction::New(0, ...)); they must keep passing the full consensus
// validation chain. The multi-node migration-sync twin runs in 21-06.
TEST_F( LegacyMintAdmissionTest, ZeroValueV1MintPassesTypeRulesAndFullChain )
{
    auto filler = MakeSignedV1Mint( 0 );

    EXPECT_TRUE( LegacyMintAdmissionTestAccess::CheckTypeRules( *tm_, *filler ) )
        << "Zero-amount v1 mints (migration nonce fillers) must stay admitted";

    const auto verdict = LegacyMintAdmissionTestAccess::ValidateForConsensus( *tm_, *filler );
    EXPECT_EQ( verdict.check, ConsensusManager::Check::Approve );
}

// VER-01 compat twin (T-21-0504: the gate must not misfire on honest v2 traffic).
// mint-v2 is the authorized mint path. It always carries UTXO parameters, so
// CheckTransactionTypeRules routes it into the pre-existing UTXO branch and the
// legacy gate is provably not reached. With a public source chain id the
// public-chain input validator accepts explicit input references without local
// ownership checks (by design — the burn lives on the external chain), so this
// construction PASSES type rules today and must keep passing after the gate.
TEST_F( LegacyMintAdmissionTest, MintV2KeepsUnchangedTypeRuleVerdict )
{
    auto mint_v2 = MakeMintV2();

    EXPECT_TRUE( mint_v2->HasUTXOParameters() )
        << "mint-v2 must keep routing to the UTXO branch, never the legacy gate";
    EXPECT_FALSE( TransactionManager::IsValueCarryingLegacyMint( *mint_v2 ) )
        << "The MINT-03 predicate must not classify mint-v2 as a legacy v1 mint";
    EXPECT_TRUE( LegacyMintAdmissionTestAccess::CheckTypeRules( *tm_, *mint_v2 ) )
        << "mint-v2 must keep passing type rules (authorized mint path)";
}

// VER-01 compat twin for non-mint traffic: a bare transfer keeps the exact
// verdict it had before the gate (UTXO branch, empty input set -> false).
TEST_F( LegacyMintAdmissionTest, TransferKeepsUnchangedTypeRuleVerdict )
{
    auto transfer = MakeTransfer();

    EXPECT_TRUE( transfer->HasUTXOParameters() );
    EXPECT_FALSE( TransactionManager::IsValueCarryingLegacyMint( *transfer ) );
    EXPECT_FALSE( LegacyMintAdmissionTestAccess::CheckTypeRules( *tm_, *transfer ) )
        << "Empty UTXO set keeps its pre-existing (false) verdict — unchanged by the gate";
}
