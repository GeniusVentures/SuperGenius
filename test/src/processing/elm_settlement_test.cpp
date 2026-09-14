/**
 * @file elm_settlement_test.cpp
 * @brief elmbridge 04-04 Task 3: settlement arithmetic matrix, envelope
 *        filter, and the BuildPayoutOutputs ELM branch + parity suites.
 *
 * Pure unit legs throughout (no node startup): suite A drives
 * ElmWindowMillihours/ElmWindowsToShares; suite B drives
 * ElmWindowFromEnvelopeJson; suite C drives TransactionManager::
 * BuildPayoutOutputs through the test-access friend (payout_outputs_test
 * pattern) with constructed TaskResults + settlement data.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "account/TransactionManager.hpp"
#include "processing/elm_settlement.hpp"

namespace sgns
{
    // Reuses the EXISTING friend declared in TransactionManager.hpp
    // (PayoutOutputsTestAccess) — payout_outputs_test.cpp defines its own
    // copy of the same class name; we define ours with the extended
    // parameter surface for the ELM branch.
    class PayoutOutputsTestAccess
    {
    public:
        static outcome::result<std::vector<OutputDestInfo>> Build( const SGProcessing::TaskResult &task_result,
                                                                   uint64_t                        escrow_amount,
                                                                   const TokenID                  &escrow_token_id,
                                                                   uint64_t                        burn_basis_points,
                                                                   const processing::ElmSettlementData *elmSettlement = nullptr,
                                                                   const std::string                    &refundAddress = "" )
        {
            return TransactionManager::BuildPayoutOutputs(
                task_result, escrow_amount, escrow_token_id, burn_basis_points, elmSettlement, refundAddress );
        }
    };
} // namespace sgns

namespace
{
    using sgns::processing::ElmSettlementData;
    using sgns::processing::ElmSubtaskWindow;
    using sgns::processing::ElmWindowFromEnvelopeJson;
    using sgns::processing::ElmWindowMillihours;
    using sgns::processing::ElmWindowsToShares;

    const sgns::TokenID ESCROW_TOKEN = sgns::TokenID::FromBytes( { 0x00 } );

    std::string Address( char digit )
    {
        return std::string( 128, digit );
    }

    // usec helpers: hours * 3.6e9.
    constexpr int64_t HoursToUsec( double h )
    {
        return static_cast<int64_t>( h * 3600.0 * 1000.0 * 1000.0 );
    }

    ElmSubtaskWindow MakeWindow( const std::string &id, double hours )
    {
        return ElmSubtaskWindow{ id, 0, HoursToUsec( hours ) };
    }

    uint64_t SumShares( const sgns::processing::ElmSplitResult &r )
    {
        uint64_t sum = 0;
        for ( const auto &s : r.shares )
        {
            sum += s.minions;
        }
        return sum;
    }

    std::map<std::string, uint64_t> SharesById( const sgns::processing::ElmSplitResult &r )
    {
        std::map<std::string, uint64_t> m;
        for ( const auto &s : r.shares )
        {
            m[ s.subtaskid ] = s.minions;
        }
        return m;
    }

    uint64_t SumOutputs( const std::vector<sgns::OutputDestInfo> &outputs )
    {
        uint64_t sum = 0;
        for ( const auto &o : outputs )
        {
            sum += o.encrypted_amount;
        }
        return sum;
    }

    void AddResult( SGProcessing::TaskResult &task_result,
                    const std::string    &subtask_id,
                    const std::string    &peer_address,
                    uint64_t              developer_cut = 0 )
    {
        auto *result = task_result.add_subtask_results();
        result->set_subtaskid( subtask_id );
        result->set_node_address( peer_address );
        result->set_developer_address( Address( 'd' ) );
        result->set_developer_cut( developer_cut );
        result->set_token_id( ESCROW_TOKEN.bytes().data(), ESCROW_TOKEN.size() );
    }

    std::vector<uint8_t> EnvelopeBytes( const std::string &json )
    {
        return std::vector<uint8_t>( json.begin(), json.end() );
    }
} // namespace

// ===================== (A) ArithmeticMatrix =====================

TEST( ElmSettlementArithmetic, MillihourConversions )
{
    // 1h -> 1000 milli-hours.
    EXPECT_EQ( ElmWindowMillihours( MakeWindow( "a", 1.0 ) ), 1000u );
    // 0.001h (exactly one milli-hour).
    EXPECT_EQ( ElmWindowMillihours( MakeWindow( "a", 0.001 ) ), 1u );
    // Sub-milli-hour rounds (llround: 0.0005h -> 0.5 milli-hour -> 1? No:
    // 0.0005h == 0.5 milli-hours, llround(0.5) == 1 (round-half-away).
    EXPECT_EQ( ElmWindowMillihours( MakeWindow( "a", 0.0005 ) ), 1u );
    // Deep sub-milli-hour -> 0.
    EXPECT_EQ( ElmWindowMillihours( MakeWindow( "a", 0.0000001 ) ), 0u );
    // Zero window -> 0.
    EXPECT_EQ( ElmWindowMillihours( ElmSubtaskWindow{ "a", 100, 100 } ), 0u );
    // Negative delta (finish < grab) -> 0, never negative.
    EXPECT_EQ( ElmWindowMillihours( ElmSubtaskWindow{ "a", 500, 100 } ), 0u );
}

TEST( ElmSettlementArithmetic, BillableFloorAndCap )
{
    // 1000 milli-hours (1h) * 3/10 == 300.
    {
        const auto r = ElmWindowsToShares( { MakeWindow( "a", 1.0 ) }, 300 );
        EXPECT_EQ( r.billableMinions, 300u );
        EXPECT_EQ( r.refundMinions, 0u );
    }
    // 4 milli-hours * 3/10 == 12/10 -> floor 1.
    {
        ElmSubtaskWindow w{ "a", 0, 4 * 3600000 }; // 4 milli-hours == 14400ms == 14.4s
        const auto r = ElmWindowsToShares( { w }, 300 );
        EXPECT_EQ( r.billableMinions, 1u );
        EXPECT_EQ( r.refundMinions, 299u );
    }
    // Cap: measured 3h (900 minions) against a 300 escrow -> 300, no extra.
    {
        const auto r = ElmWindowsToShares( { MakeWindow( "a", 3.0 ) }, 300 );
        EXPECT_EQ( r.billableMinions, 300u );
        EXPECT_EQ( r.refundMinions, 0u );
    }
}

TEST( ElmSettlementArithmetic, ProportionalSplit )
{
    // 1800:600 milli-hours (3:1... the design's 2:1 example scaled): windows
    // 1.8h and 0.6h -> 1800/600; escrow 300 -> billable 300 -> 225/75.
    const auto r = ElmWindowsToShares( { MakeWindow( "long", 1.8 ), MakeWindow( "short", 0.6 ) }, 300 );
    const auto shares = SharesById( r );
    EXPECT_EQ( r.billableMinions, 300u );
    EXPECT_EQ( shares.at( "long" ), 225u );
    EXPECT_EQ( shares.at( "short" ), 75u );
    EXPECT_EQ( r.refundMinions, 0u );
}

TEST( ElmSettlementArithmetic, RefundWhenMeasuredBelowDeclared )
{
    // Measured 30 min (500 milli-hours -> 150 minions) vs 300 declared:
    // 150 paid proportionally, 150 refunded.
    const auto r = ElmWindowsToShares( { MakeWindow( "a", 0.5 ) }, 300 );
    EXPECT_EQ( r.billableMinions, 150u );
    EXPECT_EQ( r.refundMinions, 150u );
    const auto shares = SharesById( r );
    EXPECT_EQ( shares.at( "a" ), 150u );
}

TEST( ElmSettlementArithmetic, RemainderTiebreakLexicographic )
{
    // Two equal windows "b" and "a": billable leaves a remainder -> the
    // lexicographically FIRST id ("a") receives the extra minion.
    // 1h + 1h = 2000 mh -> 600 minions -> even 300/300 (no remainder).
    // Use an amount that forces one: escrow 100 -> billable min(600,100)=100
    // -> 50/50, no remainder either. Force remainder: windows of 1h and 1h
    // with escrow 101 -> billable 101 -> floor 50/50 + remainder 1 -> "a".
    const auto r = ElmWindowsToShares( { MakeWindow( "b", 1.0 ), MakeWindow( "a", 1.0 ) }, 101 );
    const auto shares = SharesById( r );
    EXPECT_EQ( shares.at( "a" ), 51u );
    EXPECT_EQ( shares.at( "b" ), 50u );
}

TEST( ElmSettlementArithmetic, MalformedWindowsExcluded )
{
    // Negative-delta and empty-id windows are excluded from Σ and shares.
    ElmSubtaskWindow bad{ "bad", 500, 100 };   // finish < grab
    ElmSubtaskWindow noid{ "", 0, HoursToUsec( 1.0 ) };
    const auto       r = ElmWindowsToShares( { bad, noid, MakeWindow( "good", 1.0 ) }, 300 );
    const auto       shares = SharesById( r );
    EXPECT_EQ( shares.size(), 1u );
    EXPECT_EQ( shares.at( "good" ), 300u );
    EXPECT_EQ( r.billableMinions, 300u );
}

TEST( ElmSettlementArithmetic, AllMalformedYieldsFullRefund )
{
    ElmSubtaskWindow bad{ "bad", 500, 100 };
    const auto       r = ElmWindowsToShares( { bad }, 300 );
    EXPECT_TRUE( r.shares.empty() );
    EXPECT_EQ( r.refundMinions, 300u );
}

TEST( ElmSettlementArithmetic, ConservationEveryRow )
{
    struct Row
    {
        std::vector<ElmSubtaskWindow> windows;
        uint64_t                      escrow;
    };
    const std::vector<Row> rows = {
        { { MakeWindow( "a", 1.0 ) }, 300 },
        { { MakeWindow( "a", 1.8 ), MakeWindow( "b", 0.6 ) }, 300 },
        { { MakeWindow( "a", 1.0 ), MakeWindow( "b", 1.0 ), MakeWindow( "c", 0.3 ) }, 101 },
        { { MakeWindow( "a", 24.0 ), MakeWindow( "b", 0.001 ) }, 7200 },
        { { MakeWindow( "a", 0.5 ), ElmSubtaskWindow{ "bad", 9, 1 } }, 300 },
        { { ElmSubtaskWindow{ "bad", 9, 1 } }, 300 },
    };
    for ( const auto &row : rows )
    {
        const auto r = ElmWindowsToShares( row.windows, row.escrow );
        // uint128-safe via uint64 accumulation here (values are small);
        // the invariant: Σ(shares) + refund == escrow.
        EXPECT_EQ( SumShares( r ) + r.refundMinions, row.escrow )
            << "conservation broken for escrow " << row.escrow;
    }
}

// ===================== (B) EnvelopeFilter =====================

TEST( ElmSettlementEnvelopeFilter, ValidEnvelopeParses )
{
    const auto bytes = EnvelopeBytes(
        R"({"work_item_id":"w-1","text":"hi","prompt_tokens":1,"completion_tokens":1,"finish_reason":"stop","model_manifest_hash":"abc","grab_time_usec":1000,"finish_time_usec":3600001000})" );
    auto window = ElmWindowFromEnvelopeJson( bytes, "subtask-42" );
    ASSERT_TRUE( window.has_value() );
    EXPECT_EQ( window->subtaskid, "subtask-42" ); // caller-supplied key
    EXPECT_EQ( window->grab_time_usec, 1000 );
    EXPECT_EQ( window->finish_time_usec, 3600001000 );
}

TEST( ElmSettlementEnvelopeFilter, MalformedInputsYieldNullopt )
{
    // Truncated JSON.
    EXPECT_FALSE( ElmWindowFromEnvelopeJson( EnvelopeBytes( R"({"grab_time_usec": 1)" ), "s" ).has_value() );
    // Missing finish key.
    EXPECT_FALSE( ElmWindowFromEnvelopeJson( EnvelopeBytes( R"({"grab_time_usec": 1})" ), "s" ).has_value() );
    // String-typed stamps.
    EXPECT_FALSE( ElmWindowFromEnvelopeJson(
                      EnvelopeBytes( R"({"grab_time_usec": "1", "finish_time_usec": 2})" ), "s" )
                      .has_value() );
    // Not an object.
    EXPECT_FALSE( ElmWindowFromEnvelopeJson( EnvelopeBytes( R"([1,2,3])" ), "s" ).has_value() );
}

TEST( ElmSettlementEnvelopeFilter, FinishBeforeGrabIsAZeroWindow )
{
    // Parses fine; the zero/negative window is excluded downstream (S3.3).
    const auto bytes = EnvelopeBytes( R"({"grab_time_usec": 500, "finish_time_usec": 100})" );
    auto window = ElmWindowFromEnvelopeJson( bytes, "s" );
    ASSERT_TRUE( window.has_value() );
    EXPECT_EQ( ElmWindowMillihours( *window ), 0u );
}

// ===================== (C) PayoutBranch =====================

TEST( ElmPayoutBranch, ProportionalWithRefundAndConservation )
{
    const auto peer1 = Address( '1' );
    const auto peer2 = Address( '2' );

    SGProcessing::TaskResult task_result;
    AddResult( task_result, "sub-a", peer1 );
    AddResult( task_result, "sub-b", peer2 );

    // Windows 2:1 (2h : 1h) against escrow 300: billable min(900*3/10=270? no:
    // 3000 mh * 3/10 = 900, capped to 300 -> 300) -> shares 200/100; refund 0.
    // Use 1h:0.5h instead: 1500 mh * 3/10 = 450 -> capped 300 -> 200/100.
    ElmSettlementData data;
    data.windows = { MakeWindow( "sub-a", 1.0 ), MakeWindow( "sub-b", 0.5 ) };

    auto outputs = sgns::PayoutOutputsTestAccess::Build( task_result, 300, ESCROW_TOKEN, 0, &data, "0xrefund" );
    ASSERT_TRUE( outputs.has_value() );

    // Map outputs by address.
    std::map<std::string, uint64_t> byAddress;
    for ( const auto &o : outputs.value() )
    {
        byAddress[ o.dest_address ] += o.encrypted_amount;
    }
    EXPECT_EQ( byAddress[ peer1 ], 200u );
    EXPECT_EQ( byAddress[ peer2 ], 100u );
    // Conservation: Σ == escrow.
    EXPECT_EQ( SumOutputs( outputs.value() ), 300u );
}

TEST( ElmPayoutBranch, RefundOutputToRefundAddress )
{
    const auto peer1 = Address( '1' );
    SGProcessing::TaskResult task_result;
    AddResult( task_result, "sub-a", peer1 );

    // Measured 30 min vs 300 declared: 150 paid, 150 refunded.
    ElmSettlementData data;
    data.windows = { MakeWindow( "sub-a", 0.5 ) };

    auto outputs = sgns::PayoutOutputsTestAccess::Build( task_result, 300, ESCROW_TOKEN, 0, &data, "0xrefund" );
    ASSERT_TRUE( outputs.has_value() );
    std::map<std::string, uint64_t> byAddress;
    for ( const auto &o : outputs.value() )
    {
        byAddress[ o.dest_address ] += o.encrypted_amount;
    }
    EXPECT_EQ( byAddress[ peer1 ], 150u );
    EXPECT_EQ( byAddress[ "0xrefund" ], 150u );
    EXPECT_EQ( SumOutputs( outputs.value() ), 300u );
}

TEST( ElmPayoutBranch, NoWindowResultGetsZero )
{
    const auto peer1 = Address( '1' );
    const auto peer2 = Address( '2' );
    SGProcessing::TaskResult task_result;
    AddResult( task_result, "sub-a", peer1 );
    AddResult( task_result, "sub-orphan", peer2 ); // no window -> nothing

    ElmSettlementData data;
    data.windows = { MakeWindow( "sub-a", 1.0 ) }; // 300 billable

    auto outputs = sgns::PayoutOutputsTestAccess::Build( task_result, 300, ESCROW_TOKEN, 0, &data, "0xrefund" );
    ASSERT_TRUE( outputs.has_value() );
    std::map<std::string, uint64_t> byAddress;
    for ( const auto &o : outputs.value() )
    {
        byAddress[ o.dest_address ] += o.encrypted_amount;
    }
    EXPECT_EQ( byAddress[ peer1 ], 300u );
    EXPECT_EQ( byAddress.count( peer2 ), 0u ); // filtered, not blocking
    EXPECT_EQ( SumOutputs( outputs.value() ), 300u );
}

TEST( ElmPayoutBranch, AllWindowsMalformedRefuses )
{
    SGProcessing::TaskResult task_result;
    AddResult( task_result, "sub-a", Address( '1' ) );

    ElmSettlementData data;
    data.windows = { ElmSubtaskWindow{ "sub-a", 500, 100 } }; // malformed

    auto outputs = sgns::PayoutOutputsTestAccess::Build( task_result, 300, ESCROW_TOKEN, 0, &data, "0xrefund" );
    ASSERT_TRUE( outputs.has_error() );
}

TEST( ElmPayoutBranch, RefundWithoutAddressFailsClosed )
{
    SGProcessing::TaskResult task_result;
    AddResult( task_result, "sub-a", Address( '1' ) );

    ElmSettlementData data;
    data.windows = { MakeWindow( "sub-a", 0.5 ) }; // refund 150 > 0

    auto outputs = sgns::PayoutOutputsTestAccess::Build( task_result, 300, ESCROW_TOKEN, 0, &data, "" );
    ASSERT_TRUE( outputs.has_error() );
}

TEST( ElmPayoutBranch, NullSettlementReproducesEvenSplitExactly )
{
    // Parity: the SAME TaskResult through the nullptr path produces the
    // pre-change even-split outputs. Note: this file's AddResult gives every
    // result the SAME developer address, so developer credits COLLAPSE into
    // one output (the payout_outputs_test aggregation rule) — the fixture
    // there uses distinct developer addresses for separate outputs.
    SGProcessing::TaskResult task_result;
    AddResult( task_result, "subtask-a", Address( '1' ), 700'000 );
    AddResult( task_result, "subtask-b", Address( '2' ), 200'000 );

    auto outputs = sgns::PayoutOutputsTestAccess::Build( task_result, 20, ESCROW_TOKEN, 0 );
    ASSERT_TRUE( outputs.has_value() );
    // Even split: 10 each; cuts 700k -> dev 7 / peer 3; 200k -> dev 2 / peer 8.
    // Order: results in order (peer), then collapsed developer, then burn.
    ASSERT_EQ( outputs.value().size(), 4U );
    EXPECT_EQ( outputs.value()[0].encrypted_amount, 3u ); // peer a (cut 700k: dev 7, peer 3)
    EXPECT_EQ( outputs.value()[1].encrypted_amount, 8u ); // peer b (cut 200k: dev 2, peer 8)
    EXPECT_EQ( outputs.value()[2].encrypted_amount, 9u ); // dev a+b collapsed
    EXPECT_EQ( outputs.value()[3].encrypted_amount, 0u ); // burn tail
    EXPECT_EQ( SumOutputs( outputs.value() ), 20u );
}
