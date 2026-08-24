/**
 * @file       public_chain_input_validator_slot_test.cpp
 * @brief      Vote slot hashes must reflect endpoints that actually verified the
 *             claim, never merely-configured endpoints (issue #364).
 */
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/json.hpp>

#include "account/MintTransaction.hpp"
#include "account/PublicChainInputValidator.hpp"
#include "crypto/hasher.hpp"
#include "eth/json_rpc.hpp"
#include "eth/rpc_receipt_source.hpp"

using sgns::MintTransaction;
using sgns::PublicChainInputValidator;
using sgns::RpcVerificationEvidence;
using sgns::TokenID;
using sgns::WeightedRpcEndpoint;

/// @brief Friend accessor for the private evidence-gathering path.
class PublicChainInputValidatorTestAccess
{
public:
    static RpcVerificationEvidence Gather( const PublicChainInputValidator          &validator,
                                           const std::shared_ptr<sgns::GeniusTransaction> &tx,
                                           const std::string                        &source_reference )
    {
        return validator.GatherVerificationEvidence( tx, source_reference );
    }

    static void Store( const PublicChainInputValidator &validator,
                       const std::string               &claim_key,
                       RpcVerificationEvidence          evidence )
    {
        validator.StoreEvidence( claim_key, std::move( evidence ) );
    }
};

namespace
{
    constexpr const char *kChainId  = "11155111";
    constexpr const char *kContract = "0x9af8050220d8c355ca3c6dc00a78b474cd3e3c70";
    constexpr const char *kTopic0   = "0x1111111111111111111111111111111111111111111111111111111111111111";

    constexpr const char *kDirectUrl  = "https://direct.example/api-key";
    constexpr const char *kPublicUrlA = "https://public-a.example";
    constexpr const char *kPublicUrlB = "https://public-b.example";
    constexpr const char *kPublicUrlC = "https://public-c.example";

    const std::string kSourceRef = "0x" + std::string( 64, 'b' );

    std::vector<uint8_t> HashUrl( const std::string &url )
    {
        const auto digest = sgns::crypto::sha2_256( reinterpret_cast<const uint8_t *>( url.data() ), url.size() );
        return std::vector<uint8_t>( digest.begin(), digest.end() );
    }

    /// @brief Receipt JSON with a configurable status, log contract and topic0.
    std::string ReceiptJson( const std::string &contract, const std::string &topic0, bool success = true )
    {
        boost::json::object result;
        result["status"]          = success ? "0x1" : "0x0";
        result["blockNumber"]     = "0x100000";
        result["blockHash"]       = "0x" + std::string( 64, '1' );
        result["transactionHash"] = kSourceRef;

        boost::json::object log_entry;
        log_entry["address"]         = contract;
        log_entry["topics"]          = boost::json::array{ topic0 };
        log_entry["data"]            = "0x";
        log_entry["blockNumber"]     = "0x100000";
        log_entry["blockHash"]       = "0x" + std::string( 64, '1' );
        log_entry["transactionHash"] = kSourceRef;
        log_entry["logIndex"]        = "0x0";
        result["logs"]               = boost::json::array{ std::move( log_entry ) };

        boost::json::object root;
        root["jsonrpc"] = "2.0";
        root["id"]      = 1;
        root["result"]  = std::move( result );
        return boost::json::serialize( root );
    }

    /// @brief A healthy receipt matching the configured bridge contract and topic0.
    std::string GoodReceipt()
    {
        return ReceiptJson( kContract, kTopic0 );
    }

    /// @brief Transport returning a canned response, or nothing (timeout/failure).
    class CannedTransport final : public eth::rpc::JsonRpcTransport
    {
    public:
        explicit CannedTransport( std::optional<std::string> response ) : response_( std::move( response ) ) {}

        std::optional<std::string> call( const boost::json::object & ) override
        {
            return response_;
        }

    private:
        std::optional<std::string> response_;
    };

    WeightedRpcEndpoint MakeEndpoint( std::string url, uint8_t weight )
    {
        WeightedRpcEndpoint ep;
        ep.url                     = std::move( url );
        ep.consensus_weight        = weight;
        ep.bridge_contract_address = kContract;
        ep.accepted_topic0_hashes  = { kTopic0 };
        return ep;
    }

    /// @brief Validator wired with one DIRECT_API (50) + three PUBLIC (25) endpoints,
    ///        and a transport factory driven by a per-URL response table.
    /// @param responses URL → canned JSON response; a missing/nullopt entry is a failed call.
    std::unique_ptr<PublicChainInputValidator> MakeValidator( const std::unordered_map<std::string, std::optional<std::string>> &responses )
    {
        static std::unordered_map<std::string, std::optional<std::string>> table;
        table = responses;

        auto validator = std::make_unique<PublicChainInputValidator>();
        validator->SetRpcEndpoints( kChainId,
                                    { MakeEndpoint( kDirectUrl, 50 ),
                                     MakeEndpoint( kPublicUrlA, 25 ),
                                     MakeEndpoint( kPublicUrlB, 25 ),
                                     MakeEndpoint( kPublicUrlC, 25 ) } );
        validator->SetTransportFactory(
            []( const std::string &url, std::chrono::seconds ) -> std::unique_ptr<eth::rpc::JsonRpcTransport>
            {
                auto it = table.find( url );
                return std::make_unique<CannedTransport>( it == table.end() ? std::nullopt : it->second );
            } );
        return validator;
    }

    std::shared_ptr<sgns::GeniusTransaction> MakeMint()
    {
        SGTransaction::DAGStruct dag;
        return std::make_shared<MintTransaction>( MintTransaction::New( 1, kChainId, TokenID::FromBytes( { 0x00 } ), dag ) );
    }

    // ── Slot semantics on the evidence itself ───────────────────────────────

    TEST( RpcVerificationEvidenceSlotTest, EmptyEvidenceAbstainsFromEverySlot )
    {
        const RpcVerificationEvidence evidence;
        EXPECT_TRUE( evidence.SlotHash( 0 ).empty() );
        EXPECT_TRUE( evidence.SlotHash( 1 ).empty() );
        EXPECT_TRUE( evidence.SlotHash( 2 ).empty() );
    }

    TEST( RpcVerificationEvidenceSlotTest, UnknownSlotIndexAbstains )
    {
        RpcVerificationEvidence evidence;
        evidence.successful_direct_api = HashUrl( kDirectUrl );
        evidence.successful_public     = { HashUrl( kPublicUrlA ), HashUrl( kPublicUrlB ) };
        EXPECT_TRUE( evidence.SlotHash( 3 ).empty() );
        EXPECT_TRUE( evidence.SlotHash( 99 ).empty() );
    }

    TEST( RpcVerificationEvidenceSlotTest, SlotsOneAndTwoAreDistinctEndpoints )
    {
        RpcVerificationEvidence evidence;
        evidence.successful_public = { HashUrl( kPublicUrlA ), HashUrl( kPublicUrlB ) };
        EXPECT_EQ( evidence.SlotHash( 1 ), HashUrl( kPublicUrlA ) );
        EXPECT_EQ( evidence.SlotHash( 2 ), HashUrl( kPublicUrlB ) );
        EXPECT_NE( evidence.SlotHash( 1 ), evidence.SlotHash( 2 ) );
    }

    TEST( RpcVerificationEvidenceSlotTest, SingleSuccessfulPublicLeavesSlotTwoEmpty )
    {
        RpcVerificationEvidence evidence;
        evidence.successful_public = { HashUrl( kPublicUrlA ) };
        EXPECT_EQ( evidence.SlotHash( 1 ), HashUrl( kPublicUrlA ) );
        EXPECT_TRUE( evidence.SlotHash( 2 ).empty() );
    }

    // ── Evidence gathering: only verifying endpoints earn a slot ────────────

    TEST( PublicChainSlotEvidenceTest, FailedDirectApiNeverPopulatesSlotZero )
    {
        // The #364 scenario: DIRECT_API (50) fails, three PUBLIC (25) succeed → 75.
        auto validator = MakeValidator( { { kDirectUrl, std::nullopt },
                                          { kPublicUrlA, GoodReceipt() },
                                          { kPublicUrlB, GoodReceipt() },
                                          { kPublicUrlC, GoodReceipt() } } );

        const auto evidence = PublicChainInputValidatorTestAccess::Gather( *validator, MakeMint(), kSourceRef );

        EXPECT_TRUE( evidence.valid ) << "Three public endpoints reach the 75 weight quorum";
        EXPECT_EQ( evidence.successful_weight, 75 );
        EXPECT_TRUE( evidence.SlotHash( 0 ).empty() ) << "A failed DIRECT_API endpoint must never claim slot 0";
        EXPECT_EQ( evidence.SlotHash( 1 ), HashUrl( kPublicUrlA ) );
        EXPECT_EQ( evidence.SlotHash( 2 ), HashUrl( kPublicUrlB ) );
    }

    TEST( PublicChainSlotEvidenceTest, DirectApiOnlySuccessLeavesPublicSlotsEmpty )
    {
        auto validator = MakeValidator( { { kDirectUrl, GoodReceipt() },
                                          { kPublicUrlA, std::nullopt },
                                          { kPublicUrlB, std::nullopt },
                                          { kPublicUrlC, std::nullopt } } );

        const auto evidence = PublicChainInputValidatorTestAccess::Gather( *validator, MakeMint(), kSourceRef );

        EXPECT_FALSE( evidence.valid ) << "50 weight alone is below the 75 quorum";
        EXPECT_EQ( evidence.SlotHash( 0 ), HashUrl( kDirectUrl ) );
        EXPECT_TRUE( evidence.SlotHash( 1 ).empty() );
        EXPECT_TRUE( evidence.SlotHash( 2 ).empty() );
    }

    TEST( PublicChainSlotEvidenceTest, SlotsNameSuccessfulPublicEndpointsNotTheFirstConfigured )
    {
        // First configured PUBLIC endpoint fails; the later ones succeed.
        auto validator = MakeValidator( { { kDirectUrl, GoodReceipt() },
                                          { kPublicUrlA, std::nullopt },
                                          { kPublicUrlB, GoodReceipt() },
                                          { kPublicUrlC, GoodReceipt() } } );

        const auto evidence = PublicChainInputValidatorTestAccess::Gather( *validator, MakeMint(), kSourceRef );

        EXPECT_TRUE( evidence.valid );
        EXPECT_EQ( evidence.SlotHash( 0 ), HashUrl( kDirectUrl ) );
        EXPECT_EQ( evidence.SlotHash( 1 ), HashUrl( kPublicUrlB ) ) << "Slot 1 must name a successful endpoint";
        EXPECT_EQ( evidence.SlotHash( 2 ), HashUrl( kPublicUrlC ) );
        EXPECT_NE( evidence.SlotHash( 1 ), HashUrl( kPublicUrlA ) );
    }

    TEST( PublicChainSlotEvidenceTest, ReceiptWithoutMatchingBridgeEventEarnsNoSlot )
    {
        const std::string wrong_topic = "0x" + std::string( 63, 'c' ) + "1";
        auto              validator   = MakeValidator( { { kDirectUrl, ReceiptJson( kContract, wrong_topic ) },
                                                         { kPublicUrlA, GoodReceipt() },
                                                         { kPublicUrlB, GoodReceipt() },
                                                         { kPublicUrlC, GoodReceipt() } } );

        const auto evidence = PublicChainInputValidatorTestAccess::Gather( *validator, MakeMint(), kSourceRef );

        EXPECT_TRUE( evidence.SlotHash( 0 ).empty() ) << "No matching bridge event → no slot";
        EXPECT_EQ( evidence.successful_weight, 75 );
    }

    TEST( PublicChainSlotEvidenceTest, MatchingTopicFromWrongContractEarnsNoSlot )
    {
        const std::string wrong_contract = "0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
        auto              validator      = MakeValidator( { { kDirectUrl, ReceiptJson( wrong_contract, kTopic0 ) },
                                                            { kPublicUrlA, GoodReceipt() },
                                                            { kPublicUrlB, GoodReceipt() },
                                                            { kPublicUrlC, GoodReceipt() } } );

        const auto evidence = PublicChainInputValidatorTestAccess::Gather( *validator, MakeMint(), kSourceRef );

        EXPECT_TRUE( evidence.SlotHash( 0 ).empty() )
            << "A matching topic0 from a different bridge contract must not confirm";
    }

    TEST( PublicChainSlotEvidenceTest, RevertedReceiptYieldsNoEvidenceAtAll )
    {
        auto validator = MakeValidator( { { kDirectUrl, ReceiptJson( kContract, kTopic0, /*success=*/false ) },
                                          { kPublicUrlA, GoodReceipt() },
                                          { kPublicUrlB, GoodReceipt() },
                                          { kPublicUrlC, GoodReceipt() } } );

        const auto evidence = PublicChainInputValidatorTestAccess::Gather( *validator, MakeMint(), kSourceRef );

        EXPECT_FALSE( evidence.valid );
        EXPECT_EQ( evidence.successful_weight, 0 );
        EXPECT_TRUE( evidence.SlotHash( 0 ).empty() );
        EXPECT_TRUE( evidence.SlotHash( 1 ).empty() );
        EXPECT_TRUE( evidence.SlotHash( 2 ).empty() );
    }

    TEST( PublicChainSlotEvidenceTest, AllEndpointsFailingYieldsNoSlots )
    {
        auto validator = MakeValidator( {} );

        const auto evidence = PublicChainInputValidatorTestAccess::Gather( *validator, MakeMint(), kSourceRef );

        EXPECT_FALSE( evidence.valid );
        EXPECT_TRUE( evidence.SlotHash( 0 ).empty() );
        EXPECT_TRUE( evidence.SlotHash( 1 ).empty() );
        EXPECT_TRUE( evidence.SlotHash( 2 ).empty() );
    }

    // ── Claim binding ──────────────────────────────────────────────────────

    TEST( PublicChainEvidenceCacheTest, EvidenceIsConsumedOnceAndScopedToItsClaim )
    {
        PublicChainInputValidator validator;

        RpcVerificationEvidence evidence;
        evidence.valid                 = true;
        evidence.successful_direct_api = HashUrl( kDirectUrl );
        PublicChainInputValidatorTestAccess::Store( validator, "claim-A", evidence );

        // Evidence recorded for claim A must not be visible to claim B (#364).
        EXPECT_FALSE( validator.TakeEvidence( "claim-B" ).has_value() );

        const auto taken = validator.TakeEvidence( "claim-A" );
        ASSERT_TRUE( taken.has_value() );
        EXPECT_EQ( taken->SlotHash( 0 ), HashUrl( kDirectUrl ) );

        // Consumed exactly once: a replayed vote cannot reuse it.
        EXPECT_FALSE( validator.TakeEvidence( "claim-A" ).has_value() );
    }

    TEST( PublicChainEvidenceCacheTest, ConcurrentClaimsKeepSeparateEndpointEvidence )
    {
        PublicChainInputValidator validator;

        RpcVerificationEvidence direct_only;
        direct_only.valid                 = true;
        direct_only.successful_direct_api = HashUrl( kDirectUrl );

        RpcVerificationEvidence public_only;
        public_only.valid              = true;
        public_only.successful_public  = { HashUrl( kPublicUrlA ), HashUrl( kPublicUrlB ) };

        PublicChainInputValidatorTestAccess::Store( validator, "claim-A", direct_only );
        PublicChainInputValidatorTestAccess::Store( validator, "claim-B", public_only );

        const auto a = validator.TakeEvidence( "claim-A" );
        const auto b = validator.TakeEvidence( "claim-B" );
        ASSERT_TRUE( a.has_value() );
        ASSERT_TRUE( b.has_value() );

        EXPECT_EQ( a->SlotHash( 0 ), HashUrl( kDirectUrl ) );
        EXPECT_TRUE( a->SlotHash( 1 ).empty() );
        EXPECT_TRUE( b->SlotHash( 0 ).empty() );
        EXPECT_EQ( b->SlotHash( 1 ), HashUrl( kPublicUrlA ) );
    }
}
