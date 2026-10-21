/**
 * @file       mint_evidence_binding_test.cpp
 * @brief      Adversarial + compat pairs for the mint evidence layer (MINT-02 /
 *             MINT-04): fail-closed mint identities and burn-receipt fact binding.
 *
 * Every adversarial case names the attacker authority it models, the expected
 * rejection, and the pre-fix behavior (VER-01 in-phase discipline). Every
 * adversarial case has a compat twin proving the legitimate pre-upgrade path
 * survives.
 *
 * @date       2026-10-20
 */
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <boost/json.hpp>

#include "account/BridgeEventTypes.hpp"
#include "account/GeniusTransaction.hpp"
#include "account/MintTransactionV2.hpp"
#include "account/PublicChainInputValidator.hpp"
#include "base/blob.hpp"
#include "base/parse_utility.hpp"
#include "eth/abi_decoder.hpp"
#include "eth/json_rpc.hpp"
#include "eth/rpc_receipt_source.hpp"

using sgns::GeniusTransaction;
using sgns::MintTransactionV2;
using sgns::PublicChainInputValidator;
using sgns::RpcVerificationEvidence;
using sgns::TokenID;
using sgns::WeightedRpcEndpoint;

/// @brief Friend accessor for the private evidence-gathering path (mirrors the
///        slot test's accessor; one definition per test binary).
class PublicChainInputValidatorTestAccess
{
public:
    static RpcVerificationEvidence Gather( const PublicChainInputValidator &validator,
                                           const GeniusTransaction         &tx,
                                           const std::string               &source_reference )
    {
        return validator.GatherVerificationEvidence( tx, source_reference );
    }
};

namespace
{
    constexpr const char *kChainId  = "11155111";
    constexpr const char *kContract = "0x9af8050220d8c355ca3c6dc00a78b474cd3e3c70";

    constexpr const char *kPublicUrlA = "https://public-a.example";
    constexpr const char *kPublicUrlB = "https://public-b.example";
    constexpr const char *kPublicUrlC = "https://public-c.example";

    /// Burn source reference; must parse as a 32-byte hash for the RPC fetch.
    const std::string kSourceRef = "0x" + std::string( 64, 'b' );

    /// secp256k1 generator point halves (a valid public key) for v2-shape
    /// receipts: DecompressXOnlyPubkey(X, /*y_odd=*/false) returns exactly
    /// X||Y, so a v2 compat twin can carry matching facts without key machinery.
    constexpr const char *kGeneratorX = "79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798";
    constexpr const char *kGeneratorY = "483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8";

    /// @brief Canonical topic0 (0x-prefixed lowercase hex) of an event signature.
    std::string CanonicalTopic0( std::string_view signature )
    {
        const auto hash = eth::abi::event_signature_hash( std::string( signature ) );
        return rlp::base::parse::hex_bytes( hash.data(), hash.size() );
    }

    /// @brief 32-byte ABI head word for a uint256 value (64 bare hex chars).
    std::string Uint256Word( uint64_t value )
    {
        std::string           word( 64, '0' );
        static constexpr char kDigits[] = "0123456789abcdef";
        for ( size_t i = 0; i < sizeof( value ); ++i )
        {
            const auto byte = static_cast<uint8_t>( value >> ( 8 * ( sizeof( value ) - 1 - i ) ) );
            word[64 - 2 * sizeof( value ) + 2 * i]     = kDigits[byte >> 4];
            word[64 - 2 * sizeof( value ) + 2 * i + 1] = kDigits[byte & 0x0F];
        }
        return word;
    }

    /// @brief Decoded burn facts a receipt's bridge event log carries. The mint
    ///        transaction's own fields are always set independently of these.
    struct ReceiptFacts
    {
        uint64_t    token_id = 0;
        uint64_t    amount   = 1;
        std::string dest_hex; ///< v1: bare 128-hex X||Y; v2: bare 64-hex X half
        bool        dest_y_odd = false;
        bool        v2_shape   = false; ///< v2 BridgeOutInitiated vs v1 BridgeSourceBurned
        bool        success    = true;
        bool        empty_data = false; ///< Serve an undecodable matched log (data "0x")
    };

    /**
     * @brief Builds an eth_getTransactionReceipt response whose single log is a
     *        bridge burn event carrying @p facts, ABI-encoded exactly as the
     *        registered event descriptors decode it.
     *
     * v1 BridgeSourceBurned(address indexed sender, uint256 id, uint256 amount,
     *     uint256 srcChainID, uint256 destChainID, bytes sgnsDestination):
     *     data = 5 head words (id, amount, src, dest, tail offset 0x100)
     *           + tail (length 0x40, 64 destination bytes).
     * v2 BridgeOutInitiated(address indexed sender, uint256 id, uint256 amount,
     *     uint256 srcChainID, uint256 destChainID, bytes32 sgnsDestination, bool destinationYOdd):
     *     data = 6 head words (all static types).
     */
    std::string BuildBridgeBurnReceiptJson( const std::string &tx_hash, const ReceiptFacts &facts )
    {
        // 32-byte indexed-address topic: 12 bytes of padding + 20-byte address.
        static const std::string kSenderTopic = "0x" + std::string( 24, '0' ) +
                                                "1111111111111111111111111111111111111111";

        std::string data;
        if ( facts.empty_data )
        {
            data.clear();
        }
        else if ( facts.v2_shape )
        {
            data = Uint256Word( facts.token_id ) + Uint256Word( facts.amount ) + Uint256Word( 11155111 ) +
                   Uint256Word( 0 ) + facts.dest_hex + Uint256Word( facts.dest_y_odd ? 1 : 0 );
        }
        else
        {
            data = Uint256Word( facts.token_id ) + Uint256Word( facts.amount ) + Uint256Word( 11155111 ) +
                   Uint256Word( 0 ) + Uint256Word( 5 * 32 ) + Uint256Word( 64 ) + facts.dest_hex;
        }

        const std::string topic0 = CanonicalTopic0( facts.v2_shape ? sgns::kBridgeOutInitiatedSig
                                                                   : sgns::kBridgeSourceBurnedSig );

        boost::json::object result;
        result["status"]          = facts.success ? "0x1" : "0x0";
        result["blockNumber"]     = "0x100000";
        result["blockHash"]       = "0x" + std::string( 64, '1' );
        result["transactionHash"] = tx_hash;

        boost::json::object log_entry;
        log_entry["address"]         = kContract;
        log_entry["topics"]          = boost::json::array{ topic0, kSenderTopic };
        log_entry["data"]            = "0x" + data;
        log_entry["blockNumber"]     = "0x100000";
        log_entry["blockHash"]       = "0x" + std::string( 64, '1' );
        log_entry["transactionHash"] = tx_hash;
        log_entry["logIndex"]        = "0x0";
        result["logs"]               = boost::json::array{ std::move( log_entry ) };

        boost::json::object root;
        root["jsonrpc"] = "2.0";
        root["id"]      = 1;
        root["result"]  = std::move( result );
        return boost::json::serialize( root );
    }

    /// @brief Transport returning a canned response for any request.
    class CannedTransport final : public eth::rpc::JsonRpcTransport
    {
    public:
        explicit CannedTransport( std::string response ) : response_( std::move( response ) )
        {
        }

        std::optional<std::string> call( const boost::json::object & ) override
        {
            return response_;
        }

    private:
        std::string response_;
    };

    /// @brief Validator wired with three PUBLIC endpoints (3 x 25 = 75 quorum)
    ///        accepting both canonical bridge topic0 hashes, and a transport
    ///        factory serving @p receipt_json from every endpoint.
    std::unique_ptr<PublicChainInputValidator> MakeValidator( const std::string &receipt_json )
    {
        auto validator = std::make_unique<PublicChainInputValidator>();

        std::vector<WeightedRpcEndpoint> endpoints;
        for ( const auto *url : { kPublicUrlA, kPublicUrlB, kPublicUrlC } )
        {
            WeightedRpcEndpoint ep;
            ep.url                     = url;
            ep.consensus_weight        = 25;
            ep.bridge_contract_address = kContract;
            ep.accepted_topic0_hashes  = { CanonicalTopic0( sgns::kBridgeSourceBurnedSig ),
                                           CanonicalTopic0( sgns::kBridgeOutInitiatedSig ) };
            endpoints.push_back( std::move( ep ) );
        }
        validator->SetRpcEndpoints( kChainId, std::move( endpoints ) );
        validator->SetTransportFactory(
            [receipt_json]( const std::string &, std::chrono::seconds ) -> std::unique_ptr<eth::rpc::JsonRpcTransport>
            { return std::make_unique<CannedTransport>( receipt_json ); } );
        return validator;
    }

    /// @brief A mint-v2 whose own fields are set independently of any receipt.
    std::shared_ptr<MintTransactionV2> MakeMintV2( uint64_t       amount,
                                                   std::string    chain_id,
                                                   const TokenID &token_id,
                                                   std::string    destination )
    {
        auto                     burn_hash = sgns::base::Hash256::fromReadableString( std::string( 64, 'b' ) );
        SGTransaction::DAGStruct dag;
        return std::make_shared<MintTransactionV2>( MintTransactionV2::New( amount,
                                                                            std::move( chain_id ),
                                                                            token_id,
                                                                            std::move( dag ),
                                                                            { { burn_hash.value(), 0, {} } },
                                                                            std::move( destination ) ) );
    }

    /// @brief A multi-output mint-v2 built through the attacker's actual
    ///        route: a serialized MintTxV2 proto whose utxo_params.outputs
    ///        carries the primary (fact-matching) output plus one extra-value
    ///        output. DeSerializeByteVector accepts N outputs (only an empty
    ///        output set is backfilled with one), and the constructor is
    ///        private — the proto route IS the multi-output wire shape.
    std::shared_ptr<MintTransactionV2> MakeMultiOutputMintV2( uint64_t       amount,
                                                              std::string    chain_id,
                                                              const TokenID &token_id,
                                                              std::string    primary_dest,
                                                              uint64_t       extra_amount,
                                                              std::string    extra_dest )
    {
        SGTransaction::MintTxV2 proto;
        proto.set_chain_id( chain_id );
        proto.set_amount( amount );
        proto.set_token_id( token_id.bytes().data(), token_id.size() );

        auto *utxo_params = proto.mutable_utxo_params();
        auto *input       = utxo_params->add_inputs();
        input->set_tx_id_hash( std::string( 64, 'b' ) );
        input->set_output_index( 0 );

        auto *primary = utxo_params->add_outputs();
        primary->set_encrypted_amount( amount );
        primary->set_dest_addr( std::move( primary_dest ) );
        primary->set_token_id( token_id.bytes().data(), token_id.size() );

        auto *extra = utxo_params->add_outputs();
        extra->set_encrypted_amount( extra_amount );
        extra->set_dest_addr( std::move( extra_dest ) );
        extra->set_token_id( token_id.bytes().data(), token_id.size() );

        std::string serialized;
        proto.SerializeToString( &serialized );
        return MintTransactionV2::DeSerializeByteVector( std::vector<uint8_t>( serialized.begin(), serialized.end() ) );
    }

    /// @brief A non-mint transaction on the local genius chain (escrow-style):
    ///        the type family MINT-02 must keep exempt from the local bypass.
    class LocalEvidenceExemptTx final : public GeniusTransaction
    {
    public:
        LocalEvidenceExemptTx() : GeniusTransaction( "escrow-hold", SGTransaction::DAGStruct{} )
        {
        }

        std::vector<uint8_t> SerializeByteVector( const SGTransaction::DAGStruct & ) const override
        {
            return {};
        }

        sgns::EmbeddedTransaction SerializeToEmbeddedTransaction( const SGTransaction::DAGStruct & ) const override
        {
            return {};
        }

        std::string GetTransactionSpecificPath() const override
        {
            return GetType();
        }

        std::string GetChainId() const override
        {
            return "supergenius";
        }
    };

    // ── MINT-02: fail-closed identities on public-evidence-required mints ────

    TEST( MintEvidenceBindingTest, MintV2EmptySourceReferenceRejected )
    {
        // Attacker authority: a submitter controlling mint contents submits a
        // mint-v2 with an EMPTY source reference (no burn to verify against).
        // Expected: rejected (evidence.valid == false).
        // Pre-fix behavior: accepted with evidence.valid == true and every slot
        // empty — the empty-identity fail-open (finding #3 / T-21-0201).
        PublicChainInputValidator validator;
        const auto                mint = MakeMintV2( 1,
                                      kChainId,
                                      TokenID::FromBytes( { 0x00 } ),
                                      std::string( kGeneratorX ) + kGeneratorY );

        const auto evidence = PublicChainInputValidatorTestAccess::Gather( validator, *mint, "" );
        EXPECT_FALSE( evidence.valid ) << "A mint-v2 with an empty source reference must be rejected";
    }

    TEST( MintEvidenceBindingTest, MintV2EmptyChainIdRejected )
    {
        // Attacker authority: a submitter controlling mint contents submits a
        // mint-v2 with an EMPTY chain id and a non-empty source reference.
        // Expected: rejected. Pre-fix: accepted (evidence.valid == true).
        PublicChainInputValidator validator;
        const auto mint = MakeMintV2( 1, "", TokenID::FromBytes( { 0x00 } ), std::string( kGeneratorX ) + kGeneratorY );

        const auto evidence = PublicChainInputValidatorTestAccess::Gather( validator, *mint, kSourceRef );
        EXPECT_FALSE( evidence.valid ) << "A mint-v2 with an empty chain id must be rejected";
    }

    TEST( MintEvidenceBindingTest, MintV2LocalGeniusChainIdRejected )
    {
        // Attacker authority: a submitter controlling mint contents submits a
        // mint-v2 claiming the LOCAL chain id "supergenius".
        // Expected: rejected. Pre-fix: accepted via the local bypass.
        PublicChainInputValidator validator;
        const auto                mint = MakeMintV2( 1,
                                      "supergenius",
                                      TokenID::FromBytes( { 0x00 } ),
                                      std::string( kGeneratorX ) + kGeneratorY );

        const auto evidence = PublicChainInputValidatorTestAccess::Gather( validator, *mint, kSourceRef );
        EXPECT_FALSE( evidence.valid ) << "A mint-v2 on the local genius chain id must be rejected";
    }

    TEST( MintEvidenceBindingTest, MintV2ScopedLocalChainIdRejected )
    {
        // Attacker authority: a submitter controlling mint contents submits a
        // mint-v2 claiming a SCOPED local chain id ("supergenius/333", the
        // ScopedChainId private-network form). Expected: rejected by the named
        // per-type rule, not merely by the incidental no-endpoints lookup.
        // Pre-fix: rejected only because no RPC endpoints exist for the chain —
        // the rejection reason was accidental, never the per-type policy.
        PublicChainInputValidator validator;
        const auto                mint = MakeMintV2( 1,
                                      "supergenius/333",
                                      TokenID::FromBytes( { 0x00 } ),
                                      std::string( kGeneratorX ) + kGeneratorY );

        const auto evidence = PublicChainInputValidatorTestAccess::Gather( validator, *mint, kSourceRef );
        EXPECT_FALSE( evidence.valid ) << "A mint-v2 on a scoped local chain id must be rejected";
    }

    TEST( MintEvidenceBindingTest, LocalAuthorizedTypeKeepsLocalPath )
    {
        // Compat twin: escrow/registration/genesis-path transactions with local
        // chain ids keep today's accept-with-no-evidence behavior (Pitfall 7 —
        // the fail-closed rule is per-type, never a blanket chain-id rejection).
        PublicChainInputValidator validator;
        LocalEvidenceExemptTx     local_tx;

        const auto evidence = PublicChainInputValidatorTestAccess::Gather( validator, local_tx, "" );
        EXPECT_TRUE( evidence.valid ) << "A non-mint local-authorized type must keep the local bypass";
        EXPECT_TRUE( evidence.SlotHash( 0 ).empty() ) << "Local bypass still records no endpoint evidence";
    }

    TEST( MintEvidenceBindingTest, MintV2PublicChainIdProceedsToRpcVerification )
    {
        // Compat twin: a mint-v2 with a public chain id and a non-empty source
        // reference still proceeds to (and passes) RPC verification as today.
        ReceiptFacts facts;
        facts.token_id = 0;
        facts.amount   = 7;
        facts.dest_hex = std::string( kGeneratorX ) + kGeneratorY;
        facts.v2_shape = false;
        auto validator = MakeValidator( BuildBridgeBurnReceiptJson( kSourceRef, facts ) );

        const auto mint = MakeMintV2( facts.amount, kChainId, TokenID::FromBytes( { 0x00 } ), facts.dest_hex );

        const auto evidence = PublicChainInputValidatorTestAccess::Gather( *validator, *mint, kSourceRef );
        EXPECT_TRUE( evidence.valid ) << "A public-chain mint-v2 with an honest matching receipt must verify";
        EXPECT_EQ( evidence.successful_weight, 75 );
    }

    // ── MINT-04: burn-receipt facts bind to the mint's own outputs ───────────

    TEST( MintEvidenceBindingTest, ReceiptAmountMismatchRejected )
    {
        // Attacker authority: a submitter controlling mint contents mints 7
        // units against a burn receipt that honestly records 8. Every RPC
        // endpoint honestly serves the true receipt; only the mint lies.
        // Expected: rejected. Pre-fix behavior: accepted — the receipt check
        // verified only status + contract + topic0 (finding #5 / T-21-0202).
        ReceiptFacts facts;
        facts.token_id = 0xABCD;
        facts.amount   = 8;
        facts.dest_hex = std::string( kGeneratorX ) + kGeneratorY;
        auto validator = MakeValidator( BuildBridgeBurnReceiptJson( kSourceRef, facts ) );

        const auto mint = MakeMintV2( 7, kChainId, TokenID::FromBytes( { 0xAB, 0xCD } ), facts.dest_hex );

        const auto evidence = PublicChainInputValidatorTestAccess::Gather( *validator, *mint, kSourceRef );
        EXPECT_FALSE( evidence.valid ) << "A mint whose amount differs from the burned receipt must be rejected";
    }

    TEST( MintEvidenceBindingTest, ReceiptTokenMismatchRejected )
    {
        // Attacker authority: a submitter controlling mint contents mints token
        // ABCD against a burn receipt that honestly records token ABCE.
        // Expected: rejected. Pre-fix behavior: accepted.
        ReceiptFacts facts;
        facts.token_id = 0xABCE;
        facts.amount   = 7;
        facts.dest_hex = std::string( kGeneratorX ) + kGeneratorY;
        auto validator = MakeValidator( BuildBridgeBurnReceiptJson( kSourceRef, facts ) );

        const auto mint = MakeMintV2( 7, kChainId, TokenID::FromBytes( { 0xAB, 0xCD } ), facts.dest_hex );

        const auto evidence = PublicChainInputValidatorTestAccess::Gather( *validator, *mint, kSourceRef );
        EXPECT_FALSE( evidence.valid ) << "A mint whose token differs from the burned receipt must be rejected";
    }

    TEST( MintEvidenceBindingTest, ReceiptRecipientMismatchRejected )
    {
        // Attacker authority: a submitter controlling mint contents pays their
        // own address while the burn receipt honestly names a different
        // recipient. Expected: rejected. Pre-fix behavior: accepted.
        ReceiptFacts facts;
        facts.token_id = 0xABCD;
        facts.amount   = 7;
        facts.dest_hex = std::string( kGeneratorX ) + kGeneratorY;
        auto validator = MakeValidator( BuildBridgeBurnReceiptJson( kSourceRef, facts ) );

        const std::string attacker_dest( 128, 'd' );
        const auto        mint = MakeMintV2( 7, kChainId, TokenID::FromBytes( { 0xAB, 0xCD } ), attacker_dest );

        const auto evidence = PublicChainInputValidatorTestAccess::Gather( *validator, *mint, kSourceRef );
        EXPECT_FALSE( evidence.valid ) << "A mint whose recipient differs from the burned receipt must be rejected";
    }

    TEST( MintEvidenceBindingTest, MultiOutputMintFirstOutputMatchesStillRejected )
    {
        // Attacker authority: the mint submitter crafting outputs beyond the
        // attested burn builds a mint-v2 whose output[0] carries exactly the
        // receipt's decoded facts (token, amount, destination) and whose
        // output[1] carries extra value to any destination. Every fact
        // comparison reads output[0] only, so even honest bridge-configured
        // endpoints serving the true receipt must not confirm the claim: the
        // burn attests exactly one output, not "output[0] plus anything".
        // Expected: rejected (all evidence discarded).
        // Pre-fix behavior (CR-01, 21-VERIFICATION gap 1): valid == true with
        // weight 75/75 — every check passed on output[0] while the mint
        // creates two outputs.
        ReceiptFacts facts;
        facts.token_id = 0xABCD;
        facts.amount   = 7;
        facts.dest_hex = std::string( kGeneratorX ) + kGeneratorY;
        auto validator = MakeValidator( BuildBridgeBurnReceiptJson( kSourceRef, facts ) );

        const std::string attacker_dest( 128, 'e' );
        const auto        mint = MakeMultiOutputMintV2( 7,
                                                 kChainId,
                                                 TokenID::FromBytes( { 0xAB, 0xCD } ),
                                                 facts.dest_hex,
                                                 1000,
                                                 attacker_dest );
        ASSERT_NE( mint, nullptr ) << "The proto route must deserialize the two-output mint";
        ASSERT_EQ( mint->GetUTXOParameters().second.size(), 2u ) << "Fixture must carry the multi-output attack shape";
        EXPECT_EQ( mint->GetAmount(), 7u ) << "Primary output reads the attested amount";
        EXPECT_EQ( mint->GetPrimaryDestinationAddress(), facts.dest_hex )
            << "Primary output reads the attested destination";

        const auto evidence = PublicChainInputValidatorTestAccess::Gather( *validator, *mint, kSourceRef );
        EXPECT_FALSE( evidence.valid )
            << "A multi-output mint whose output[0] matches the burn must be rejected (CR-01)";
    }

    TEST( MintEvidenceBindingTest, MatchingV2FactsValidate )
    {
        // Compat twin (v2 event shape): a BridgeOutInitiated receipt whose
        // decompressed destination equals the mint's destination, with token
        // and amount matching, still validates after fact binding.
        ReceiptFacts facts;
        facts.token_id   = 0xABCD;
        facts.amount     = 7;
        facts.dest_hex   = kGeneratorX;
        facts.dest_y_odd = false;
        facts.v2_shape   = true;
        auto validator   = MakeValidator( BuildBridgeBurnReceiptJson( kSourceRef, facts ) );

        const auto mint = MakeMintV2( 7,
                                      kChainId,
                                      TokenID::FromBytes( { 0xAB, 0xCD } ),
                                      std::string( kGeneratorX ) + kGeneratorY );

        const auto evidence = PublicChainInputValidatorTestAccess::Gather( *validator, *mint, kSourceRef );
        EXPECT_TRUE( evidence.valid ) << "A v2 receipt whose decoded facts match the mint must verify";
        EXPECT_EQ( evidence.decoded_amount, 7u ) << "Evidence carries the decoded amount for Phase 24 corroboration";
        EXPECT_EQ( evidence.decoded_token_id, TokenID::FromBytes( { 0xAB, 0xCD } ) );
        EXPECT_EQ( evidence.decoded_recipient, std::string( kGeneratorX ) + kGeneratorY );
    }

    TEST( MintEvidenceBindingTest, V2RecipientParityMismatchRejected )
    {
        // Attacker authority (v2 shape): the receipt's destinationYOdd names the
        // opposite Y parity, so the decompressed recipient is the mirrored
        // point — not the mint's destination. Expected: rejected.
        ReceiptFacts facts;
        facts.token_id   = 0xABCD;
        facts.amount     = 7;
        facts.dest_hex   = kGeneratorX;
        facts.dest_y_odd = true; // generator Y is even — flipped parity decodes a different recipient
        facts.v2_shape   = true;
        auto validator   = MakeValidator( BuildBridgeBurnReceiptJson( kSourceRef, facts ) );

        const auto mint = MakeMintV2( 7,
                                      kChainId,
                                      TokenID::FromBytes( { 0xAB, 0xCD } ),
                                      std::string( kGeneratorX ) + kGeneratorY );

        const auto evidence = PublicChainInputValidatorTestAccess::Gather( *validator, *mint, kSourceRef );
        EXPECT_FALSE( evidence.valid ) << "A v2 receipt whose parity-decoded recipient differs must be rejected";
    }

    TEST( MintEvidenceBindingTest, UndecodableMatchedLogRejected )
    {
        // Attacker authority (decode-layer bypass attempt): a receipt whose log
        // matches the bridge contract and topic0 but carries no decodable event
        // data. Expected: fail closed. Pre-fix behavior: accepted.
        ReceiptFacts facts;
        facts.token_id   = 0xABCD;
        facts.amount     = 7;
        facts.dest_hex   = std::string( kGeneratorX ) + kGeneratorY;
        facts.empty_data = true;
        auto validator   = MakeValidator( BuildBridgeBurnReceiptJson( kSourceRef, facts ) );

        const auto mint = MakeMintV2( 7, kChainId, TokenID::FromBytes( { 0xAB, 0xCD } ), facts.dest_hex );

        const auto evidence = PublicChainInputValidatorTestAccess::Gather( *validator, *mint, kSourceRef );
        EXPECT_FALSE( evidence.valid ) << "A matched log that cannot be decoded must fail closed (T-21-0204)";
    }

    TEST( MintEvidenceBindingTest, BogusTopic0ReceiptStillRejected )
    {
        // Negative control: a receipt whose log carries a topic0 that is NOT in
        // the endpoint's accepted set earns no weight. The fact-binding fix must
        // not weaken the existing receipt-log validation. Behavior is unchanged
        // by this fix (rejected before and after).
        ReceiptFacts facts;
        facts.token_id = 0xABCD;
        facts.amount   = 7;
        facts.dest_hex = std::string( kGeneratorX ) + kGeneratorY;
        auto receipt   = BuildBridgeBurnReceiptJson( kSourceRef, facts );
        // Swap the log's topic0 for one outside the accepted set.
        const std::string bogus_topic0 = "0x" + std::string( 63, 'c' ) + "1";
        const auto        topics_pos   = receipt.find( "\"topics\":[" );
        const auto        first_quote  = receipt.find( '"', topics_pos + 9 ) + 1;
        const auto        last_quote   = receipt.find( '"', first_quote );
        receipt                        = receipt.substr( 0, first_quote ) + bogus_topic0 + receipt.substr( last_quote );
        auto validator                 = MakeValidator( receipt );

        const auto mint = MakeMintV2( 7, kChainId, TokenID::FromBytes( { 0xAB, 0xCD } ), facts.dest_hex );

        const auto evidence = PublicChainInputValidatorTestAccess::Gather( *validator, *mint, kSourceRef );
        EXPECT_FALSE( evidence.valid ) << "A receipt with an unknown topic0 must still be rejected";
        EXPECT_EQ( evidence.successful_weight, 0 ) << "No endpoint may confirm a bogus-topic0 receipt";
    }
} // namespace
