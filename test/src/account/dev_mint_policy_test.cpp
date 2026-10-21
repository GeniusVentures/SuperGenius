/**
 * @file       dev_mint_policy_test.cpp
 * @brief      D-03 net-scoped dev-mint policy proofs: the evidence bypass legs
 *             permit burn-less mints only on designated nets, the dev
 *             validator's verdict follows the validator's OWN net regardless
 *             of witness content, and a "dev"-chain mint routes through the
 *             existing input-validator registry.
 * @details    The process-global net id (version::GetNetworkID) defaults to
 *             DEV_NET_ID 144 (designated), so every fixture that switches nets
 *             saves the current id in SetUp and restores it in TearDown — the
 *             id is shared by every suite in this binary. The evidence-bypass
 *             suites drive PublicChainInputValidator::GatherVerificationEvidence
 *             through the slot-test friend-accessor pattern; the validator
 *             suites use a real Blockchain reference (the verdict call shape)
 *             on the CRDTFixture stack.
 * @date       2026-10-21
 */

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "account/DevMintInputValidator.hpp"
#include "account/GeniusAccount.hpp"
#include "account/InputValidators.hpp"
#include "account/MintTransactionV2.hpp"
#include "account/PublicChainInputValidator.hpp"
#include "base/DevMintPolicy.hpp"
#include "base/sgns_version.hpp"
#include "blockchain/Blockchain.hpp"
#include "blockchain/Consensus.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/storage/base_crdt_test.hpp"

using namespace sgns;

/// @brief Friend accessor for the private evidence-gathering path (the
///        public_chain_input_validator_slot_test pattern, per-binary definition).
class PublicChainInputValidatorTestAccess
{
public:
    static RpcVerificationEvidence Gather( const PublicChainInputValidator &validator,
                                           const GeniusTransaction          &tx,
                                           const std::string                &source_reference )
    {
        return validator.GatherVerificationEvidence( tx, source_reference );
    }
};

namespace
{
    const TokenID kDevMintToken = TokenID::FromBytes( { 0x00 } );

    /// Fabricated burn hash (the multi_node_finality_fault_test shape): no
    /// public chain ever backs it.
    const std::string kBurnHashReadable = std::string( 64, 'c' );

    /// A non-empty external source reference, shaped like a public-chain tx hash.
    const std::string kSourceReference = "0x" + std::string( 64, 'b' );

    std::shared_ptr<MintTransactionV2> MintWithChain( std::string chain_id )
    {
        SGTransaction::DAGStruct dag;
        dag.set_type( "mint-v2" );
        const auto burn_hash = base::Hash256::fromReadableString( kBurnHashReadable );
        dag.set_uncle_hash( burn_hash.value().toReadableString() );
        return std::make_shared<MintTransactionV2>( MintTransactionV2::New(
            42, std::move( chain_id ), kDevMintToken, std::move( dag ), { { burn_hash.value(), 0, {} } },
            "dev-mint-destination" ) );
    }
} // namespace

/**
 * @brief Single-node fixture for the D-03 unit proofs. Saves the process-global
 *        net id in SetUp and restores it in TearDown so the per-test net
 *        switching never leaks into other suites sharing this binary.
 */
class DevMintPolicyUnitTest : public ::test::CRDTFixture
{
public:
    DevMintPolicyUnitTest() : ::test::CRDTFixture( "dev_mint_policy_test" )
    {
    }

    void SetUp() override
    {
        saved_net_ = version::GetNetworkID();
        GeniusAccount::SetSecureStorageFactory(
            []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage> {
                return std::make_shared<MemorySecureStorage>( identifier );
            } );
        account_ = GeniusAccount::New( kDevMintToken, base_path / "account" );
        ASSERT_NE( account_, nullptr );
        blockchain_ = Blockchain::New( db_, account_, pubs_, []( outcome::result<void> ) {} );
        ASSERT_NE( blockchain_, nullptr );
    }

    void TearDown() override
    {
        version::SetNetworkId( saved_net_ );
    }

    ~DevMintPolicyUnitTest() override
    {
        if ( blockchain_ )
        {
            (void) blockchain_->Stop();
        }
        blockchain_.reset();
        account_.reset();
    }

    IInputValidator::WitnessVerdict DevValidatorVerdictFor( const GeniusTransaction &tx ) const
    {
        const DevMintInputValidator validator;
        const ConsensusSubject      subject;
        const UTXOTxParameters      params;
        return validator.ValidateWitness( subject, tx, params, *blockchain_ );
    }

    uint16_t                       saved_net_ = version::DEV_NET_ID;
    std::shared_ptr<GeniusAccount> account_;
    std::shared_ptr<Blockchain>    blockchain_;
};

// ── Policy predicate: exactly the designated nets ──────────────────────────

TEST_F( DevMintPolicyUnitTest, PolicyPermitsExactlyTheDesignatedNets )
{
    for ( const auto permitted : { version::STAGING_NET_ID, version::DEV_NET_ID } )
    {
        version::SetNetworkId( permitted );
        EXPECT_TRUE( DevMintBurnlessPermitted() ) << "net " << permitted << " must permit dev mints";
    }
    for ( const auto refused : { version::TEST_NET_ID, version::MAIN_NET_ID } )
    {
        version::SetNetworkId( refused );
        EXPECT_FALSE( DevMintBurnlessPermitted() ) << "net " << refused << " must refuse dev mints";
    }
}

// ── Evidence bypass: both legs net-scoped, default-invalid on refusal ──────

TEST_F( DevMintPolicyUnitTest, EvidenceBypassEmptySourceReferencePermitsOnlyOnDesignatedNets )
{
    const PublicChainInputValidator validator; // no RPC endpoints wired
    const auto                      mint = MintWithChain( "dev" );

    for ( const auto designated : { version::STAGING_NET_ID, version::DEV_NET_ID } )
    {
        version::SetNetworkId( designated );
        const auto evidence = PublicChainInputValidatorTestAccess::Gather( validator, *mint, "" );
        EXPECT_TRUE( evidence.valid ) << "net " << designated;
    }
    for ( const auto refused : { version::TEST_NET_ID, version::MAIN_NET_ID } )
    {
        version::SetNetworkId( refused );
        const auto evidence = PublicChainInputValidatorTestAccess::Gather( validator, *mint, "" );
        EXPECT_FALSE( evidence.valid ) << "net " << refused;
    }
}

TEST_F( DevMintPolicyUnitTest, EvidenceBypassEmptyChainIdPermitsOnlyOnDesignatedNets )
{
    const PublicChainInputValidator validator; // no RPC endpoints wired
    const auto                      mint = MintWithChain( "" ); // the actually-reachable hole

    for ( const auto designated : { version::STAGING_NET_ID, version::DEV_NET_ID } )
    {
        version::SetNetworkId( designated );
        const auto evidence = PublicChainInputValidatorTestAccess::Gather( validator, *mint, kSourceReference );
        EXPECT_TRUE( evidence.valid ) << "net " << designated;
    }
    for ( const auto refused : { version::TEST_NET_ID, version::MAIN_NET_ID } )
    {
        version::SetNetworkId( refused );
        const auto evidence = PublicChainInputValidatorTestAccess::Gather( validator, *mint, kSourceReference );
        EXPECT_FALSE( evidence.valid ) << "net " << refused;
        EXPECT_TRUE( evidence.successful_public.empty() ) << "refusal must not fabricate endpoint slots";
    }
}

TEST_F( DevMintPolicyUnitTest, EvidenceBypassSupergeniusChainIdPermitsOnlyOnDesignatedNets )
{
    const PublicChainInputValidator validator; // no RPC endpoints wired
    const auto                      mint = MintWithChain( "supergenius" );

    for ( const auto designated : { version::STAGING_NET_ID, version::DEV_NET_ID } )
    {
        version::SetNetworkId( designated );
        const auto evidence = PublicChainInputValidatorTestAccess::Gather( validator, *mint, kSourceReference );
        EXPECT_TRUE( evidence.valid ) << "net " << designated;
    }
    for ( const auto refused : { version::TEST_NET_ID, version::MAIN_NET_ID } )
    {
        version::SetNetworkId( refused );
        const auto evidence = PublicChainInputValidatorTestAccess::Gather( validator, *mint, kSourceReference );
        EXPECT_FALSE( evidence.valid ) << "net " << refused;
    }
}

// ── Dev validator: verdict follows the validator's OWN net ─────────────────

TEST_F( DevMintPolicyUnitTest, DevValidatorWitnessVerdictFollowsOwnNetRegardlessOfWitnessContent )
{
    const auto mint = MintWithChain( "dev" ); // identical witness bytes for every net

    for ( const auto designated : { version::STAGING_NET_ID, version::DEV_NET_ID } )
    {
        version::SetNetworkId( designated );
        EXPECT_EQ( DevValidatorVerdictFor( *mint ), IInputValidator::WitnessVerdict::kValid )
            << "net " << designated;
    }
    for ( const auto refused : { version::TEST_NET_ID, version::MAIN_NET_ID } )
    {
        version::SetNetworkId( refused );
        EXPECT_EQ( DevValidatorVerdictFor( *mint ), IInputValidator::WitnessVerdict::kInvalid )
            << "net " << refused;
    }
}

TEST_F( DevMintPolicyUnitTest, DevValidatorRequiresNoConsensusUtxoDataOnEveryNet )
{
    const DevMintInputValidator validator;
    EXPECT_FALSE( validator.RequiresConsensusUTXOData() );
    version::SetNetworkId( version::TEST_NET_ID );
    EXPECT_FALSE( validator.RequiresConsensusUTXOData() );
    version::SetNetworkId( version::MAIN_NET_ID );
    EXPECT_FALSE( validator.RequiresConsensusUTXOData() );
}

TEST_F( DevMintPolicyUnitTest, DevValidatorAcceptsNonEmptyUtxoParameters )
{
    const DevMintInputValidator validator;
    const auto                  mint   = MintWithChain( "dev" );
    const auto                  params = mint->GetUTXOParameters();
    EXPECT_FALSE( params.first.empty() );
    EXPECT_FALSE( params.second.empty() );
    EXPECT_TRUE( validator.ValidateUTXOParameters( params, "dev-mint-source", account_->GetUTXOManager() ) );
}

TEST_F( DevMintPolicyUnitTest, DevChainMintRoutesToDevValidatorThroughRegistry )
{
    // Routing-order proof: no RPC endpoint claims "dev" (SelectInputValidator
    // step 1 misses) and the process-global registry (step 2) resolves the
    // chain id to a validator whose verdict follows the net policy.
    const PublicChainInputValidator public_validator;
    EXPECT_FALSE( public_validator.GetFirstRpcUrl( "dev" ).has_value() );

    const auto *const registered = IInputValidator::Get( "dev" );
    ASSERT_NE( registered, nullptr ) << "the dev validator must be registered for the literal chain id \"dev\"";

    const auto             mint = MintWithChain( "dev" );
    const ConsensusSubject subject;
    const UTXOTxParameters params;

    version::SetNetworkId( version::STAGING_NET_ID );
    EXPECT_EQ( registered->ValidateWitness( subject, *mint, params, *blockchain_ ),
               IInputValidator::WitnessVerdict::kValid );
    version::SetNetworkId( version::MAIN_NET_ID );
    EXPECT_EQ( registered->ValidateWitness( subject, *mint, params, *blockchain_ ),
               IInputValidator::WitnessVerdict::kInvalid );
}
