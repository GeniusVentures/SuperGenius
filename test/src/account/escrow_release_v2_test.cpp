/**
 * @file escrow_release_v2_test.cpp
 * @brief Release protocol wire, codec and canonical identity regressions.
 */

#include <gtest/gtest.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/dynamic_message.h>

#include "account/proto/SGTransaction.pb.h"
#include "blockchain/impl/proto/Consensus.pb.h"

namespace
{
    TEST( EscrowReleaseWireContractTest, AdditiveTagTenPreservesLegacyTagSeven )
    {
        const auto                    *legacy        = SGTransaction::EscrowReleaseTx::descriptor();
        const std::vector<std::string> legacy_fields = { "dag_struct",
                                                         "utxo_params",
                                                         "release_amount",
                                                         "release_address",
                                                         "escrow_source",
                                                         "original_escrow_hash" };
        ASSERT_EQ( legacy->field_count(), legacy_fields.size() );
        for ( size_t index = 0; index < legacy_fields.size(); ++index )
        {
            const auto *field = legacy->FindFieldByName( legacy_fields[index] );
            ASSERT_NE( field, nullptr );
            EXPECT_EQ( field->number(), index + 1 );
        }

        const auto                    *embedded  = sgns::EmbeddedTransaction::descriptor();
        const std::vector<std::string> old_cases = { "transfer",
                                                     "mint_v2",
                                                     "mint",
                                                     "processing",
                                                     "migration",
                                                     "escrow",
                                                     "escrow_release",
                                                     "registration",
                                                     "revoke" };
        for ( size_t index = 0; index < old_cases.size(); ++index )
        {
            ASSERT_NE( embedded->FindFieldByName( old_cases[index] ), nullptr );
            EXPECT_EQ( embedded->FindFieldByName( old_cases[index] )->number(), index + 1 );
        }
        const auto *v2_case = embedded->FindFieldByName( "escrow_release_v2" );
        ASSERT_NE( v2_case, nullptr );
        EXPECT_EQ( v2_case->number(), 10 );
        EXPECT_EQ( v2_case->containing_oneof(), embedded->FindOneofByName( "transaction" ) );
        const auto *v2 = v2_case->message_type();
        EXPECT_EQ( v2->full_name(), "SGTransaction.EscrowReleaseTxV2" );
        ASSERT_EQ( v2->field_count(), 2 );
        EXPECT_EQ( v2->FindFieldByName( "dag_struct" )->number(), 1 );
        EXPECT_EQ( v2->FindFieldByName( "utxo_params" )->number(), 2 );

        SGTransaction::EscrowReleaseTx marker;
        marker.mutable_dag_struct()->set_type( "escrow-release" );
        marker.set_release_amount( 17 );
        marker.set_release_address( "recipient" );
        SGTransaction::EscrowReleaseTx restored_marker;
        ASSERT_TRUE( restored_marker.ParseFromString( marker.SerializeAsString() ) );
        EXPECT_EQ( restored_marker.release_amount(), 17 );
        EXPECT_EQ( restored_marker.release_address(), "recipient" );

        google::protobuf::DynamicMessageFactory    factory;
        std::unique_ptr<google::protobuf::Message> payment( factory.GetPrototype( v2 )->New() );
        auto *dag = payment->GetReflection()->MutableMessage( payment.get(), v2->FindFieldByName( "dag_struct" ) );
        dag->GetReflection()->SetString( dag, dag->GetDescriptor()->FindFieldByName( "type" ), "escrow-release-v2" );
        std::unique_ptr<google::protobuf::Message> restored_payment( payment->New() );
        ASSERT_TRUE( restored_payment->ParseFromString( payment->SerializeAsString() ) );
        const auto &restored_dag = restored_payment->GetReflection()->GetMessage( *restored_payment,
                                                                                  v2->FindFieldByName( "dag_struct" ) );
        EXPECT_EQ( restored_dag.GetReflection()->GetString( restored_dag,
                                                            restored_dag.GetDescriptor()->FindFieldByName( "type" ) ),
                   "escrow-release-v2" );
    }
}
