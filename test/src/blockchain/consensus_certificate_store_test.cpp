#include <gtest/gtest.h>

#include "crdt/crdt_data_filter.hpp"
#include "testutil/storage/base_crdt_test.hpp"

namespace sgns::test
{
    namespace
    {
        constexpr std::string_view kV2Pattern = "^/?cert/v2/(slot|tx)/[0-9a-f]{64}$";
        constexpr std::string_view kSlot =
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
        constexpr std::string_view kTx =
            "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";

        crdt::pb::Delta MakePairDelta()
        {
            crdt::pb::Delta delta;
            auto           *slot = delta.add_elements();
            slot->set_key( "/cert/v2/slot/" + std::string( kSlot ) );
            slot->set_value( "certificate" );
            auto *index = delta.add_elements();
            index->set_key( "/cert/v2/tx/" + std::string( kTx ) );
            index->set_value( std::string( kSlot ) );
            return delta;
        }

        bool HasCompletePair( const crdt::pb::Delta &delta )
        {
            bool has_slot  = false;
            bool has_index = false;
            for ( const auto &element : delta.elements() )
            {
                has_slot |= element.key() == "/cert/v2/slot/" + std::string( kSlot ) &&
                            element.value() == "certificate";
                has_index |= element.key() == "/cert/v2/tx/" + std::string( kTx ) &&
                             element.value() == kSlot;
            }
            return has_slot && has_index;
        }
    } // namespace

    class ConsensusCertificateStoreTest : public ::test::CRDTFixture
    {
    public:
        ConsensusCertificateStoreTest() : CRDTFixture( "ConsensusCertificateStoreTest" )
        {
        }
    };

    TEST_F( ConsensusCertificateStoreTest, DeltaFilterAcceptsValidPair )
    {
        crdt::CRDTDataFilter filter( db_->GetWorkJournal() );
        ASSERT_TRUE( filter.RegisterDeltaFilter( std::string( kV2Pattern ), HasCompletePair ) );

        auto delta = MakePairDelta();
        filter.FilterElementsOnDelta( delta );

        ASSERT_EQ( delta.elements_size(), 2 );
        EXPECT_TRUE( HasCompletePair( delta ) );
    }

    TEST_F( ConsensusCertificateStoreTest, DeltaFilterRejectsPartialPairAtomically )
    {
        crdt::CRDTDataFilter filter( db_->GetWorkJournal() );
        ASSERT_TRUE( filter.RegisterDeltaFilter( std::string( kV2Pattern ), HasCompletePair ) );

        auto delta = MakePairDelta();
        delta.mutable_elements()->DeleteSubrange( 1, 1 );
        auto *unrelated = delta.add_elements();
        unrelated->set_key( "/unrelated/value" );
        unrelated->set_value( "preserved" );

        filter.FilterElementsOnDelta( delta );

        ASSERT_EQ( delta.elements_size(), 1 );
        EXPECT_EQ( delta.elements( 0 ).key(), "/unrelated/value" );
    }

    TEST_F( ConsensusCertificateStoreTest, PairFilterSeesCompleteDeltaBeforeElementFilters )
    {
        crdt::CRDTDataFilter filter( db_->GetWorkJournal() );
        bool                 saw_complete_pair = false;
        ASSERT_TRUE( filter.RegisterDeltaFilter(
            std::string( kV2Pattern ),
            [&saw_complete_pair]( const crdt::pb::Delta &delta )
            {
                saw_complete_pair = HasCompletePair( delta );
                return saw_complete_pair;
            } ) );
        ASSERT_TRUE( filter.RegisterElementFilter(
            "^/?cert/v2/tx/[0-9a-f]{64}$",
            []( const crdt::pb::Element & )
            { return std::optional<std::vector<crdt::pb::Element>>( std::vector<crdt::pb::Element>{} ); } ) );

        auto delta = MakePairDelta();
        filter.FilterElementsOnDelta( delta );

        EXPECT_TRUE( saw_complete_pair );
        ASSERT_EQ( delta.elements_size(), 1 );
        EXPECT_EQ( delta.elements( 0 ).key(), "/cert/v2/slot/" + std::string( kSlot ) );
    }
} // namespace sgns::test
