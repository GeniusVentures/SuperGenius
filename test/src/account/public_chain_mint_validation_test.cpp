// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <boost/json.hpp>

#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <eth/abi_decoder.hpp>
#include <eth/rpc_receipt_source.hpp>

#include "account/BridgeEventTypes.hpp"
#include "account/MintTransactionV2.hpp"
#include "account/PublicChainInputValidator.hpp"
#include "base/parse_utility.hpp"

class PublicChainInputValidatorTestAccess
{
public:
    static bool Verify( const sgns::PublicChainInputValidator          &validator,
                        const std::shared_ptr<sgns::GeniusTransaction> &tx,
                        const std::string                              &source_reference )
    {
        return validator.VerifyPublicChainSmartContract( tx, source_reference );
    }
};

namespace
{
    constexpr uint64_t kChainId = 11155111;
    constexpr uint64_t kAmount = 1000;
    constexpr uint64_t kTokenId = 42;
    const std::string kBurnHash( 64, 'a' );
    const std::string kDestination( 128, 'b' );
    const std::string kBridgeAddress = "0x1234567890123456789012345678901234567890";
    const std::string kOtherAddress = "0x9999999999999999999999999999999999999999";

    std::string HexWord( uint64_t value )
    {
        std::ostringstream out;
        out << std::hex << std::setfill( '0' ) << std::setw( 64 ) << value;
        return out.str();
    }

    std::string BurnData( uint64_t token_id,
                          uint64_t amount,
                          uint64_t source_chain,
                          const std::string &destination )
    {
        return "0x" + HexWord( token_id )
             + HexWord( amount )
             + HexWord( source_chain )
             + HexWord( 8453 )
             + HexWord( 160 )
             + HexWord( 64 )
             + destination;
    }

    struct ReceiptLog
    {
        std::string address;
        std::string topic0;
        std::string data;
    };

    std::string BridgeTopic0()
    {
        const auto hash = eth::abi::event_signature_hash( std::string( sgns::kBridgeSourceBurnedSig ) );
        return rlp::base::parse::hex_bytes( hash.data(), hash.size() );
    }

    std::string ReceiptJson( const std::vector<ReceiptLog> &logs )
    {
        const std::string tx_hash = "0x" + kBurnHash;
        const std::string block_hash = "0x" + std::string( 64, '1' );

        boost::json::object root;
        root["jsonrpc"] = "2.0";
        root["id"] = 1;

        boost::json::object result;
        result["status"] = "0x1";
        result["blockNumber"] = "0x10";
        result["blockHash"] = block_hash;
        result["transactionHash"] = tx_hash;

        boost::json::array encoded_logs;
        uint32_t block_log_index = 37;
        for ( const auto &log : logs )
        {
            boost::json::object encoded;
            encoded["address"] = log.address;
            encoded["topics"] = boost::json::array{
                log.topic0,
                "0x" + std::string( 24, '0' ) + std::string( 40, '1' ),
            };
            encoded["data"] = log.data;
            encoded["blockNumber"] = "0x10";
            encoded["blockHash"] = block_hash;
            encoded["transactionHash"] = tx_hash;
            std::ostringstream index;
            index << "0x" << std::hex << block_log_index++;
            encoded["logIndex"] = index.str();
            encoded_logs.emplace_back( std::move( encoded ) );
        }
        result["logs"] = std::move( encoded_logs );
        root["result"] = std::move( result );
        return boost::json::serialize( root );
    }

    class FixedReceiptTransport final : public eth::rpc::JsonRpcTransport
    {
    public:
        explicit FixedReceiptTransport( std::string response ) : response_( std::move( response ) )
        {
        }

        std::optional<std::string> call( const boost::json::object & ) override
        {
            return response_;
        }

    private:
        std::string response_;
    };

    std::shared_ptr<sgns::MintTransactionV2> MakeMint( uint32_t           receipt_index,
                                                       uint64_t           token_id = kTokenId,
                                                       uint64_t           amount = kAmount,
                                                       std::string        chain_id = std::to_string( kChainId ),
                                                       const std::string &destination = kDestination )
    {
        auto source_hash = sgns::base::Hash256::fromReadableString( kBurnHash );
        EXPECT_TRUE( source_hash.has_value() );

        SGTransaction::DAGStruct dag;
        dag.set_source_addr( kDestination );
        dag.set_nonce( 1 );
        dag.set_uncle_hash( kBurnHash );

        std::vector<sgns::InputUTXOInfo> inputs{
            { source_hash.value(), receipt_index, {} },
        };
        const auto token = sgns::TokenID::FromUint256(
            intx::uint256( token_id ), sgns::TokenID::Endianness::BIG );
        return std::make_shared<sgns::MintTransactionV2>(
            sgns::MintTransactionV2::New(
                amount, std::move( chain_id ), token, std::move( dag ), std::move( inputs ), destination ) );
    }

    sgns::PublicChainInputValidator ValidatorFor( std::string receipt_json,
                                                   std::string address = kBridgeAddress,
                                                   std::string topic = BridgeTopic0() )
    {
        sgns::PublicChainInputValidator validator;
        sgns::WeightedRpcEndpoint endpoint;
        endpoint.url = "mock://receipt";
        endpoint.consensus_weight = 75;
        endpoint.bridge_contract_address = std::move( address );
        endpoint.accepted_topic0_hashes = { std::move( topic ) };
        validator.SetRpcEndpoints( std::to_string( kChainId ), { std::move( endpoint ) } );
        validator.SetTransportFactory(
            [receipt_json = std::move( receipt_json )]( const std::string &, std::chrono::seconds )
            {
                return std::make_unique<FixedReceiptTransport>( receipt_json );
            } );
        return validator;
    }

    ReceiptLog ValidBurn( uint64_t token_id = kTokenId,
                          uint64_t amount = kAmount,
                          uint64_t source_chain = kChainId,
                          std::string destination = kDestination )
    {
        return { kBridgeAddress,
                 BridgeTopic0(),
                 BurnData( token_id, amount, source_chain, destination ) };
    }
} // namespace

TEST( PublicChainMintValidationTest, SelectsOnlyTheIndexedLog )
{
    auto validator = ValidatorFor( ReceiptJson( {
        ValidBurn( kTokenId, kAmount + 1 ),
        { kOtherAddress, std::string( "0x" ) + std::string( 64, 'c' ), "0x" },
        ValidBurn(),
    } ) );

    EXPECT_TRUE( PublicChainInputValidatorTestAccess::Verify(
        validator, MakeMint( 2 ), kBurnHash ) );
    EXPECT_FALSE( PublicChainInputValidatorTestAccess::Verify(
        validator, MakeMint( 0 ), kBurnHash ) );
}

TEST( PublicChainMintValidationTest, RejectsOutOfRangeWrongAddressAndWrongTopic )
{
    const auto valid_receipt = ReceiptJson( { ValidBurn() } );
    auto validator = ValidatorFor( valid_receipt );
    EXPECT_FALSE( PublicChainInputValidatorTestAccess::Verify(
        validator, MakeMint( 4 ), kBurnHash ) );

    auto wrong_address = ValidatorFor( valid_receipt, kOtherAddress );
    EXPECT_FALSE( PublicChainInputValidatorTestAccess::Verify(
        wrong_address, MakeMint( 0 ), kBurnHash ) );

    auto wrong_topic = ValidatorFor( valid_receipt, kBridgeAddress, "0x" + std::string( 64, 'd' ) );
    EXPECT_FALSE( PublicChainInputValidatorTestAccess::Verify(
        wrong_topic, MakeMint( 0 ), kBurnHash ) );
}

TEST( PublicChainMintValidationTest, RejectsEveryCandidateControlledMismatch )
{
    auto validator = ValidatorFor( ReceiptJson( { ValidBurn() } ) );

    EXPECT_FALSE( PublicChainInputValidatorTestAccess::Verify(
        validator, MakeMint( 0, kTokenId + 1 ), kBurnHash ) );
    EXPECT_FALSE( PublicChainInputValidatorTestAccess::Verify(
        validator, MakeMint( 0, kTokenId, kAmount + 1 ), kBurnHash ) );
    EXPECT_FALSE( PublicChainInputValidatorTestAccess::Verify(
        validator, MakeMint( 0, kTokenId, kAmount, std::to_string( kChainId + 1 ) ), kBurnHash ) );
    EXPECT_FALSE( PublicChainInputValidatorTestAccess::Verify(
        validator, MakeMint( 0, kTokenId, kAmount, std::to_string( kChainId ), std::string( 128, 'c' ) ),
        kBurnHash ) );
}

TEST( PublicChainMintValidationTest, SameSlotDoesNotAuthorizeMalformedCandidate )
{
    auto validator = ValidatorFor( ReceiptJson( { ValidBurn() } ) );
    const auto valid = MakeMint( 0 );
    const auto malformed = MakeMint( 0, kTokenId, kAmount + 1 );

    ASSERT_TRUE( valid->GetSlotID().has_value() );
    ASSERT_TRUE( malformed->GetSlotID().has_value() );
    EXPECT_EQ( valid->GetSlotID().value(), malformed->GetSlotID().value() );
    EXPECT_TRUE( PublicChainInputValidatorTestAccess::Verify( validator, valid, kBurnHash ) );
    EXPECT_FALSE( PublicChainInputValidatorTestAccess::Verify( validator, malformed, kBurnHash ) );
}
