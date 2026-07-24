// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/eth_receipt_source.hpp>
#include <eth/event_filter.hpp>
#include <eth/rpc_receipt_source.hpp>
#include <gtest/gtest.h>

#include "account/BridgeEventTypes.hpp"
#include "account/GeniusNode.hpp"
#include "account/TransactionManager.hpp"
#include "base/parse_utility.hpp"
#include "storage/database_error.hpp"
#include "watcher/impl/bridge_catchup_watcher.hpp"

#include <boost/json.hpp>

#include <cstdint>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sgns::evmwatcher
{
    class BridgeCatchupWatcherTestAccess
    {
    public:
        static void PollOnce( BridgeCatchupWatcher &watcher )
        {
            watcher.running.store( true );
            watcher.poll_once();
            watcher.running.store( false );
        }
    };
} // namespace sgns::evmwatcher

namespace sgns
{
    class GeniusNodeCatchupTestAccess
    {
    public:
        enum class Submission
        {
            NotAttempted,
            Succeeded,
            Failed
        };

        static evmwatcher::BridgeCatchupWatcher::BurnProcessOutcome Classify(
            bool account_available,
            bool transaction_manager_available,
            outcome::result<TransactionManager::BridgeBurnState> burn_state,
            Submission submission )
        {
            GeniusNode::CatchupSubmissionState internal_submission =
                GeniusNode::CatchupSubmissionState::NotAttempted;
            if ( submission == Submission::Succeeded )
            {
                internal_submission = GeniusNode::CatchupSubmissionState::Succeeded;
            }
            else if ( submission == Submission::Failed )
            {
                internal_submission = GeniusNode::CatchupSubmissionState::Failed;
            }
            return GeniusNode::ClassifyCatchupBurnOutcome(
                GeniusNode::CatchupBurnFacts{
                    account_available,
                    transaction_manager_available,
                    std::move( burn_state ),
                    internal_submission } );
        }
    };
} // namespace sgns

namespace
{
    constexpr uint64_t kCatchupChainId = 11155111;
    constexpr uint64_t kCatchupBlock = 10;
    const std::string kCatchupBridgeAddress = "0x1234567890123456789012345678901234567890";
    const std::string kCatchupBlockHash( 64, 'b' );
    const std::string kCatchupDestination( 128, 'c' );

    template <typename Array>
    Array Filled( uint8_t seed )
    {
        Array value{};
        for ( size_t i = 0; i < value.size(); ++i )
        {
            value[i] = static_cast<uint8_t>( seed + i );
        }
        return value;
    }

    eth::codec::LogEntry MakeLog( const eth::codec::Address &address,
                                  const eth::codec::Hash256 &topic )
    {
        eth::codec::LogEntry log;
        log.address = address;
        log.topics.push_back( topic );
        return log;
    }

    std::vector<eth::MatchedEvent> ObserveReceipt( uint32_t first_block_log_index )
    {
        const auto bridge_address = Filled<eth::codec::Address>( 0x20 );
        const auto other_address = Filled<eth::codec::Address>( 0x40 );
        const auto burn_topic = Filled<eth::codec::Hash256>( 0x60 );

        eth::codec::Receipt receipt;
        receipt.status = true;
        receipt.logs = {
            MakeLog( other_address, Filled<eth::codec::Hash256>( 0x70 ) ),
            MakeLog( bridge_address, burn_topic ),
            MakeLog( other_address, Filled<eth::codec::Hash256>( 0x80 ) ),
            MakeLog( bridge_address, burn_topic ),
        };

        eth::EventFilter filter;
        filter.addresses.push_back( bridge_address );
        filter.topics.push_back( burn_topic );

        std::vector<eth::MatchedEvent> observed;
        eth::EventWatcher watcher;
        watcher.watch( std::move( filter ),
                       [&]( const eth::MatchedEvent &event ) { observed.push_back( event ); } );
        watcher.process_receipt( receipt,
                                 Filled<eth::codec::Hash256>( 0x90 ),
                                 123,
                                 Filled<eth::codec::Hash256>( 0xa0 ),
                                 first_block_log_index );
        return observed;
    }

    std::string HexWord( uint64_t value )
    {
        std::ostringstream out;
        out << std::hex << std::setfill( '0' ) << std::setw( 64 ) << value;
        return out.str();
    }

    std::string TopicHex( std::string_view signature )
    {
        const auto topic = eth::abi::event_signature_hash( std::string( signature ) );
        return rlp::base::parse::hex_bytes( topic.data(), topic.size() );
    }

    struct CatchupLog
    {
        std::string tx_hash;
        uint32_t    log_index = 0;
        bool        is_v2 = false;
        uint64_t    block_number = kCatchupBlock;
        std::string block_hash = kCatchupBlockHash;
        bool        decodable = true;
    };

    std::string BurnDataV1()
    {
        return "0x" + HexWord( 42 )
             + HexWord( 1000 )
             + HexWord( kCatchupChainId )
             + HexWord( 8453 )
             + HexWord( 160 )
             + HexWord( 64 )
             + kCatchupDestination;
    }

    std::string BurnDataV2()
    {
        return "0x" + HexWord( 42 )
             + HexWord( 1000 )
             + HexWord( kCatchupChainId )
             + HexWord( 8453 )
             + std::string( 64, 'c' )
             + HexWord( 0 );
    }

    boost::json::object EncodeRpcLog( const CatchupLog &log )
    {
        boost::json::object encoded;
        encoded["address"] = kCatchupBridgeAddress;
        encoded["topics"] = boost::json::array{
            TopicHex( log.is_v2 ? sgns::kBridgeOutInitiatedSig : sgns::kBridgeSourceBurnedSig ),
            "0x" + std::string( 24, '0' ) + std::string( 40, '1' ),
        };
        encoded["data"] = log.decodable
                            ? ( log.is_v2 ? BurnDataV2() : BurnDataV1() )
                            : "0x01";
        encoded["blockNumber"] = "0x" + HexWord( log.block_number ).substr( 63 );
        encoded["blockHash"] = "0x" + log.block_hash;
        encoded["transactionHash"] = "0x" + log.tx_hash;

        std::ostringstream log_index;
        log_index << "0x" << std::hex << log.log_index;
        encoded["logIndex"] = log_index.str();
        return encoded;
    }

    std::string LogsJson( const std::vector<CatchupLog> &logs )
    {
        boost::json::object root;
        root["jsonrpc"] = "2.0";
        root["id"] = 1;
        boost::json::array result;
        for ( const auto &log : logs )
        {
            result.emplace_back( EncodeRpcLog( log ) );
        }
        root["result"] = std::move( result );
        return boost::json::serialize( root );
    }

    std::string ReceiptJson( const std::string             &receipt_tx_hash,
                             const std::vector<CatchupLog> &logs,
                             uint64_t                       block_number = kCatchupBlock,
                             const std::string             &block_hash = kCatchupBlockHash,
                             bool                           malformed_log_array = false )
    {
        boost::json::object root;
        root["jsonrpc"] = "2.0";
        root["id"] = 1;

        boost::json::object result;
        result["status"] = "0x1";
        result["blockNumber"] = "0x" + HexWord( block_number ).substr( 63 );
        result["blockHash"] = "0x" + block_hash;
        result["transactionHash"] = "0x" + receipt_tx_hash;

        boost::json::array encoded_logs;
        for ( const auto &log : logs )
        {
            encoded_logs.emplace_back( EncodeRpcLog( log ) );
        }
        if ( malformed_log_array )
        {
            auto malformed = EncodeRpcLog( logs.front() );
            malformed.erase( "logIndex" );
            encoded_logs.emplace_back( std::move( malformed ) );
        }
        result["logs"] = std::move( encoded_logs );
        root["result"] = std::move( result );
        return boost::json::serialize( root );
    }

    std::string BlockNumberJson()
    {
        boost::json::object root;
        root["jsonrpc"] = "2.0";
        root["id"] = 99;
        boost::json::object result;
        result["number"] = "0xa";
        root["result"] = std::move( result );
        return boost::json::serialize( root );
    }

    struct CatchupAttempt
    {
        std::optional<std::string> v1_logs;
        std::optional<std::string> v2_logs;
        std::unordered_map<std::string, std::optional<std::string>> receipts;
    };

    struct CatchupScript
    {
        std::vector<CatchupAttempt> attempts;
        size_t next_attempt = 0;
        std::vector<std::unordered_map<std::string, size_t>> receipt_calls;
    };

    class ScriptedCatchupTransport final : public eth::rpc::JsonRpcTransport
    {
    public:
        ScriptedCatchupTransport( std::shared_ptr<CatchupScript> script, size_t attempt )
            : script_( std::move( script ) ), attempt_( attempt )
        {
        }

        std::optional<std::string> call( const boost::json::object &request ) override
        {
            const std::string method( request.at( "method" ).as_string() );
            if ( method == "eth_getBlockByNumber" )
            {
                return BlockNumberJson();
            }

            auto &attempt = script_->attempts.at( attempt_ );
            if ( method == "eth_getLogs" )
            {
                return log_query_count_++ == 0 ? attempt.v1_logs : attempt.v2_logs;
            }

            if ( method == "eth_getTransactionReceipt" )
            {
                std::string tx_hash( request.at( "params" ).as_array().at( 0 ).as_string() );
                if ( tx_hash.rfind( "0x", 0 ) == 0 )
                {
                    tx_hash.erase( 0, 2 );
                }
                ++script_->receipt_calls.at( attempt_ )[tx_hash];
                const auto receipt = attempt.receipts.find( tx_hash );
                return receipt == attempt.receipts.end() ? std::nullopt : receipt->second;
            }
            return std::nullopt;
        }

    private:
        std::shared_ptr<CatchupScript> script_;
        size_t                         attempt_ = 0;
        size_t                         log_query_count_ = 0;
    };

    struct DeliveredBurn
    {
        std::string tx_hash;
        uint32_t    receipt_log_index = 0;

        bool operator<( const DeliveredBurn &other ) const
        {
            return std::tie( tx_hash, receipt_log_index )
                 < std::tie( other.tx_hash, other.receipt_log_index );
        }
    };

    class CatchupHarness
    {
    public:
        using Outcome =
            sgns::evmwatcher::BridgeCatchupWatcher::BurnProcessOutcome;

        explicit CatchupHarness(
            std::vector<CatchupAttempt> attempts,
            std::vector<std::optional<Outcome>> scripted_outcomes = {} )
            : script_( std::make_shared<CatchupScript>() ),
              scripted_outcomes_( std::move( scripted_outcomes ) )
        {
            script_->attempts = std::move( attempts );
            script_->receipt_calls.resize( script_->attempts.size() );

            sgns::evmwatcher::BridgeCatchupWatcher::Config config;
            config.start_block = kCatchupBlock;
            config.max_blocks_per_query = 1;
            config.max_chunks = 1;
            config.transport_factory = [script = script_]( const std::string & )
            {
                const size_t attempt = script->next_attempt++;
                return std::make_unique<ScriptedCatchupTransport>( script, attempt );
            };

            watcher_ = std::make_unique<sgns::evmwatcher::BridgeCatchupWatcher>(
                config,
                []( const std::string & ) {},
                []()
                {
                    return std::vector<sgns::ChainContractPair>{
                        { "scripted-chain", kCatchupBridgeAddress, kCatchupChainId },
                    };
                },
                []( const std::string & ) -> std::optional<std::string>
                {
                    return "scripted://catchup";
                },
                [this]( const std::vector<eth::abi::AbiValue> &,
                        const std::string                     &tx_hash,
                        const std::string                     &,
                        uint32_t                               receipt_log_index )
                {
                    delivered_.push_back( { tx_hash, receipt_log_index } );
                    if ( next_outcome_ >= scripted_outcomes_.size() )
                    {
                        return Outcome::Processed;
                    }
                    const auto &scripted = scripted_outcomes_[next_outcome_++];
                    if ( !scripted.has_value() )
                    {
                        throw std::runtime_error( "scripted burn processor failure" );
                    }
                    return scripted.value();
                } );
        }

        void PollOnce()
        {
            sgns::evmwatcher::BridgeCatchupWatcherTestAccess::PollOnce( *watcher_ );
        }

        uint64_t Cursor() const
        {
            return watcher_->GetLastProcessedBlock( kCatchupChainId );
        }

        const std::vector<DeliveredBurn> &Delivered() const
        {
            return delivered_;
        }

        size_t ReceiptCalls( size_t attempt, const std::string &tx_hash ) const
        {
            const auto &calls = script_->receipt_calls.at( attempt );
            const auto it = calls.find( tx_hash );
            return it == calls.end() ? 0 : it->second;
        }

    private:
        std::shared_ptr<CatchupScript> script_;
        std::unique_ptr<sgns::evmwatcher::BridgeCatchupWatcher> watcher_;
        std::vector<DeliveredBurn> delivered_;
        std::vector<std::optional<Outcome>> scripted_outcomes_;
        size_t next_outcome_ = 0;
    };
} // namespace

TEST( BridgeEventIdentityTest, ReceiptLocalOrdinalIncludesUnrelatedLogs )
{
    const auto observed = ObserveReceipt( 41 );

    ASSERT_EQ( observed.size(), 2u );
    EXPECT_EQ( observed[0].log_index, 42u );
    EXPECT_EQ( observed[1].log_index, 44u );
    ASSERT_TRUE( observed[0].receipt_log_index.has_value() );
    ASSERT_TRUE( observed[1].receipt_log_index.has_value() );
    EXPECT_EQ( *observed[0].receipt_log_index, 1u );
    EXPECT_EQ( *observed[1].receipt_log_index, 3u );
}

TEST( BridgeEventIdentityTest, ReInclusionChangesBlockIndexButNotReceiptOrdinal )
{
    const auto first = ObserveReceipt( 8 );
    const auto re_included = ObserveReceipt( 107 );

    ASSERT_EQ( first.size(), 2u );
    ASSERT_EQ( re_included.size(), 2u );
    EXPECT_NE( first[0].log_index, re_included[0].log_index );
    EXPECT_NE( first[1].log_index, re_included[1].log_index );
    EXPECT_EQ( first[0].receipt_log_index, re_included[0].receipt_log_index );
    EXPECT_EQ( first[1].receipt_log_index, re_included[1].receipt_log_index );
}

TEST( BridgeEventIdentityTest, BlockWideObservationCannotInventReceiptOrdinal )
{
    const auto bridge_address = Filled<eth::codec::Address>( 0x20 );
    const auto burn_topic = Filled<eth::codec::Hash256>( 0x60 );

    eth::EventFilter filter;
    filter.addresses.push_back( bridge_address );
    filter.topics.push_back( burn_topic );

    std::optional<eth::MatchedEvent> observed;
    eth::EventWatcher watcher;
    watcher.watch( std::move( filter ),
                   [&]( const eth::MatchedEvent &event ) { observed = event; } );
    watcher.process_block_logs( { MakeLog( bridge_address, burn_topic ) },
                                123,
                                Filled<eth::codec::Hash256>( 0xa0 ) );

    ASSERT_TRUE( observed.has_value() );
    EXPECT_FALSE( observed->receipt_log_index.has_value() );
}

TEST( BridgeEventIdentityTest, PersistenceIdentityIncludesReceiptOrdinal )
{
    const std::string burn_hash( 64, 'a' );
    const auto index_zero = sgns::TransactionManager::MakeBridgeExecutedKey( "11155111", burn_hash, 0 );
    const auto index_two = sgns::TransactionManager::MakeBridgeExecutedKey( "11155111", burn_hash, 2 );

    EXPECT_NE( index_zero, index_two );
    EXPECT_EQ( index_zero, "/bridge/executed/11155111:" + burn_hash + ":0" );
    EXPECT_EQ( index_two, "/bridge/executed/11155111:" + burn_hash + ":2" );
}

TEST( BridgeCatchupReceiptOrdinalTest, CheckedNarrowingCoversUint32Boundary )
{
    const auto zero = eth::checked_receipt_log_ordinal( uint64_t{ 0 } );
    const auto maximum = eth::checked_receipt_log_ordinal(
        uint64_t{ std::numeric_limits<uint32_t>::max() } );
    const auto overflow = eth::checked_receipt_log_ordinal(
        uint64_t{ std::numeric_limits<uint32_t>::max() } + 1 );

    ASSERT_TRUE( zero.has_value() );
    ASSERT_TRUE( maximum.has_value() );
    EXPECT_EQ( *zero, uint32_t{ 0 } );
    EXPECT_EQ( *maximum, std::numeric_limits<uint32_t>::max() );
    EXPECT_FALSE( overflow.has_value() );
}

TEST( BridgeCatchupReceiptRetryTest, LateReceiptFailurePublishesNothingAndRetriesFresh )
{
    const std::string tx_a( 64, 'a' );
    const std::string tx_b( 64, 'd' );
    const CatchupLog log_a{ tx_a, 30, false };
    const CatchupLog log_b{ tx_b, 31, false };
    const auto v1_logs = LogsJson( { log_a, log_b } );
    const auto empty_v2 = LogsJson( {} );

    CatchupHarness harness( {
        {
            v1_logs,
            empty_v2,
            {
                { tx_a, ReceiptJson( tx_a, { log_a } ) },
                { tx_b, std::nullopt },
            },
        },
        {
            v1_logs,
            empty_v2,
            {
                { tx_a, ReceiptJson( tx_a, { log_a } ) },
                { tx_b, ReceiptJson( tx_b, { log_b } ) },
            },
        },
    } );

    harness.PollOnce();
    EXPECT_TRUE( harness.Delivered().empty() );
    EXPECT_EQ( harness.Cursor(), kCatchupBlock );

    harness.PollOnce();
    EXPECT_EQ( harness.Cursor(), kCatchupBlock + 1 );
    EXPECT_EQ( harness.ReceiptCalls( 0, tx_a ), 1u );
    EXPECT_EQ( harness.ReceiptCalls( 0, tx_b ), 1u );
    EXPECT_EQ( harness.ReceiptCalls( 1, tx_a ), 1u );
    EXPECT_EQ( harness.ReceiptCalls( 1, tx_b ), 1u );

    const std::set<DeliveredBurn> delivered(
        harness.Delivered().begin(), harness.Delivered().end() );
    EXPECT_EQ( delivered.size(), 2u );
    EXPECT_EQ( harness.Delivered().size(), 2u );
    EXPECT_EQ( delivered.count( { tx_a, 0 } ), 1u );
    EXPECT_EQ( delivered.count( { tx_b, 0 } ), 1u );
}

TEST( BridgeCatchupCursorRetryTest, V1SuccessAndV2FailureCommitTogetherOnRetry )
{
    const std::string tx_v1( 64, 'e' );
    const std::string tx_v2( 64, 'f' );
    const CatchupLog log_v1{ tx_v1, 40, false };
    const CatchupLog log_v2{ tx_v2, 41, true };
    const auto v1_logs = LogsJson( { log_v1 } );
    const auto v2_logs = LogsJson( { log_v2 } );

    CatchupHarness harness( {
        {
            v1_logs,
            std::string( "{\"jsonrpc\":\"2.0\",\"result\":" ),
            {
                { tx_v1, ReceiptJson( tx_v1, { log_v1 } ) },
            },
        },
        {
            v1_logs,
            v2_logs,
            {
                { tx_v1, ReceiptJson( tx_v1, { log_v1 } ) },
                { tx_v2, ReceiptJson( tx_v2, { log_v2 } ) },
            },
        },
    } );

    harness.PollOnce();
    EXPECT_TRUE( harness.Delivered().empty() );
    EXPECT_EQ( harness.Cursor(), kCatchupBlock );

    harness.PollOnce();
    EXPECT_EQ( harness.Cursor(), kCatchupBlock + 1 );
    const std::set<DeliveredBurn> delivered(
        harness.Delivered().begin(), harness.Delivered().end() );
    EXPECT_EQ( delivered.size(), 2u );
    EXPECT_EQ( harness.Delivered().size(), 2u );
    EXPECT_EQ( delivered.count( { tx_v1, 0 } ), 1u );
    EXPECT_EQ( delivered.count( { tx_v2, 0 } ), 1u );
}

TEST( BridgeCatchupReceiptDecodeTest, UndecodableMatchingLogFailsChunk )
{
    const std::string tx_hash( 64, '5' );
    CatchupLog undecodable{ tx_hash, 49, false };
    undecodable.decodable = false;

    CatchupHarness harness( {
        {
            LogsJson( { undecodable } ),
            LogsJson( {} ),
            {
                { tx_hash, ReceiptJson( tx_hash, { undecodable } ) },
            },
        },
    } );

    harness.PollOnce();
    EXPECT_TRUE( harness.Delivered().empty() );
    EXPECT_EQ( harness.Cursor(), kCatchupBlock );
}

TEST( BridgeCatchupPublicationOutcomeTest, RetryPreservesCursorAndDedup )
{
    using Outcome =
        sgns::evmwatcher::BridgeCatchupWatcher::BurnProcessOutcome;
    const std::string tx_hash( 64, '2' );
    const CatchupLog burn{ tx_hash, 52, false };
    const CatchupAttempt attempt{
        LogsJson( { burn } ),
        LogsJson( {} ),
        { { tx_hash, ReceiptJson( tx_hash, { burn } ) } },
    };
    CatchupHarness harness(
        { attempt, attempt },
        { Outcome::Retry, Outcome::Processed } );

    harness.PollOnce();
    EXPECT_EQ( harness.Cursor(), kCatchupBlock );
    ASSERT_EQ( harness.Delivered().size(), 1U );

    harness.PollOnce();
    EXPECT_EQ( harness.Cursor(), kCatchupBlock + 1 );
    ASSERT_EQ( harness.Delivered().size(), 2U );
    EXPECT_EQ( harness.Delivered().front().tx_hash,
               harness.Delivered().back().tx_hash );
}

TEST( BridgeCatchupPublicationOutcomeTest, ExceptionPreservesCursorAndDedup )
{
    using Outcome =
        sgns::evmwatcher::BridgeCatchupWatcher::BurnProcessOutcome;
    const std::string tx_hash( 64, '3' );
    const CatchupLog burn{ tx_hash, 53, false };
    const CatchupAttempt attempt{
        LogsJson( { burn } ),
        LogsJson( {} ),
        { { tx_hash, ReceiptJson( tx_hash, { burn } ) } },
    };
    CatchupHarness harness(
        { attempt, attempt },
        { std::nullopt, Outcome::AlreadyHandled } );

    harness.PollOnce();
    EXPECT_EQ( harness.Cursor(), kCatchupBlock );
    ASSERT_EQ( harness.Delivered().size(), 1U );

    harness.PollOnce();
    EXPECT_EQ( harness.Cursor(), kCatchupBlock + 1 );
    EXPECT_EQ( harness.Delivered().size(), 2U );
}

TEST( BridgeCatchupPublicationOutcomeTest, PartialProcessedThenRetryDoesNotCommitChunk )
{
    using Outcome =
        sgns::evmwatcher::BridgeCatchupWatcher::BurnProcessOutcome;
    const std::string tx_a( 64, '4' );
    const std::string tx_b( 64, '5' );
    const CatchupLog burn_a{ tx_a, 54, false };
    const CatchupLog burn_b{ tx_b, 55, false };
    const CatchupAttempt attempt{
        LogsJson( { burn_a, burn_b } ),
        LogsJson( {} ),
        {
            { tx_a, ReceiptJson( tx_a, { burn_a } ) },
            { tx_b, ReceiptJson( tx_b, { burn_b } ) },
        },
    };
    CatchupHarness harness(
        { attempt, attempt },
        {
            Outcome::Processed,
            Outcome::Retry,
            Outcome::AlreadyHandled,
            Outcome::Processed,
        } );

    harness.PollOnce();
    EXPECT_EQ( harness.Cursor(), kCatchupBlock );
    ASSERT_EQ( harness.Delivered().size(), 2U );

    harness.PollOnce();
    EXPECT_EQ( harness.Cursor(), kCatchupBlock + 1 );
    ASSERT_EQ( harness.Delivered().size(), 4U );
    EXPECT_EQ( harness.Delivered()[0].tx_hash, harness.Delivered()[2].tx_hash );
    EXPECT_EQ( harness.Delivered()[1].tx_hash, harness.Delivered()[3].tx_hash );
}

TEST( BridgeCatchupPublicationOutcomeTest, GeniusNodeClassifierMapsTransientStatesToRetry )
{
    using Access = sgns::GeniusNodeCatchupTestAccess;
    using BurnState = sgns::TransactionManager::BridgeBurnState;
    using Outcome =
        sgns::evmwatcher::BridgeCatchupWatcher::BurnProcessOutcome;

    EXPECT_EQ( Access::Classify(
                   false, true, BurnState::Available, Access::Submission::NotAttempted ),
               Outcome::Retry );
    EXPECT_EQ( Access::Classify(
                   true, false, BurnState::Available, Access::Submission::NotAttempted ),
               Outcome::Retry );
    EXPECT_EQ( Access::Classify(
                   true,
                   true,
                   outcome::failure( sgns::storage::DatabaseError::IO_ERROR ),
                   Access::Submission::NotAttempted ),
               Outcome::Retry );
    EXPECT_EQ( Access::Classify(
                   true, true, BurnState::Reserved, Access::Submission::NotAttempted ),
               Outcome::Retry );
    EXPECT_EQ( Access::Classify(
                   true, true, BurnState::Available, Access::Submission::NotAttempted ),
               Outcome::Retry );
    EXPECT_EQ( Access::Classify(
                   true, true, BurnState::Available, Access::Submission::Failed ),
               Outcome::Retry );
}

TEST( BridgeCatchupPublicationOutcomeTest, GeniusNodeClassifierMapsSubmissionAndDurableCompletion )
{
    using Access = sgns::GeniusNodeCatchupTestAccess;
    using BurnState = sgns::TransactionManager::BridgeBurnState;
    using Outcome =
        sgns::evmwatcher::BridgeCatchupWatcher::BurnProcessOutcome;

    EXPECT_EQ( Access::Classify(
                   true, true, BurnState::Available, Access::Submission::Succeeded ),
               Outcome::Processed );
    EXPECT_EQ( Access::Classify(
                   true, true, BurnState::AlreadyHandled, Access::Submission::NotAttempted ),
               Outcome::AlreadyHandled );
    EXPECT_EQ( Access::Classify(
                   true, true, BurnState::AlreadyHandled, Access::Submission::NotAttempted ),
               Outcome::AlreadyHandled );
    EXPECT_EQ( Access::Classify(
                   true, true, BurnState::AlreadyHandled, Access::Submission::Failed ),
               Outcome::AlreadyHandled );
}

enum class ReceiptFailure
{
    kTransactionMismatch,
    kBlockHashMismatch,
    kBlockNumberMismatch,
    kMalformedLogArrays,
    kMissingLogIndex,
    kDuplicateLogIndex,
};

class BridgeCatchupReceiptMismatchTest : public ::testing::TestWithParam<ReceiptFailure>
{
};

TEST_P( BridgeCatchupReceiptMismatchTest, FailedIdentityPublishesNothingAndPreservesCursor )
{
    const std::string tx_hash( 64, '6' );
    const CatchupLog observed{ tx_hash, 50, false };
    std::string receipt;

    switch ( GetParam() )
    {
        case ReceiptFailure::kTransactionMismatch:
            receipt = ReceiptJson( std::string( 64, '7' ), { observed } );
            break;
        case ReceiptFailure::kBlockHashMismatch:
            receipt = ReceiptJson(
                tx_hash, { observed }, kCatchupBlock, std::string( 64, '8' ) );
            break;
        case ReceiptFailure::kBlockNumberMismatch:
            receipt = ReceiptJson( tx_hash, { observed }, kCatchupBlock + 1 );
            break;
        case ReceiptFailure::kMalformedLogArrays:
            receipt = ReceiptJson(
                tx_hash, { observed }, kCatchupBlock, kCatchupBlockHash, true );
            break;
        case ReceiptFailure::kMissingLogIndex:
        {
            auto different = observed;
            different.log_index = observed.log_index + 1;
            receipt = ReceiptJson( tx_hash, { different } );
            break;
        }
        case ReceiptFailure::kDuplicateLogIndex:
            receipt = ReceiptJson( tx_hash, { observed, observed } );
            break;
    }

    CatchupHarness harness( {
        {
            LogsJson( { observed } ),
            LogsJson( {} ),
            {
                { tx_hash, receipt },
            },
        },
    } );

    harness.PollOnce();
    EXPECT_TRUE( harness.Delivered().empty() );
    EXPECT_EQ( harness.Cursor(), kCatchupBlock );
}

INSTANTIATE_TEST_SUITE_P(
    ReceiptIdentityFailures,
    BridgeCatchupReceiptMismatchTest,
    ::testing::Values(
        ReceiptFailure::kTransactionMismatch,
        ReceiptFailure::kBlockHashMismatch,
        ReceiptFailure::kBlockNumberMismatch,
        ReceiptFailure::kMalformedLogArrays,
        ReceiptFailure::kMissingLogIndex,
        ReceiptFailure::kDuplicateLogIndex ) );
