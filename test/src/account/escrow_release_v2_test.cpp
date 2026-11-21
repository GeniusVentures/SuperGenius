/**
 * @file escrow_release_v2_test.cpp
 * @brief Release protocol wire, codec and canonical identity regressions.
 */

#include <gtest/gtest.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/dynamic_message.h>

#include "account/proto/SGTransaction.pb.h"
#include "account/GeniusTransaction.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"
#include "crypto/hasher.hpp"

namespace
{
    using namespace sgns;

    template <typename Message>
    std::vector<uint8_t> MessageBytes( const Message &message )
    {
        const auto bytes = message.SerializeAsString();
        return { bytes.begin(), bytes.end() };
    }

    std::shared_ptr<GeniusTransaction> DecodeReleaseV2( const std::vector<uint8_t> &bytes )
    {
        const auto &decoders = GeniusTransaction::GetDeSerializers();
        const auto  decoder  = decoders.find( "escrow-release-v2" );
        return decoder == decoders.end() ? nullptr : decoder->second( bytes );
    }

    SGTransaction::EscrowReleaseTxV2 SignedPaymentWire( GeniusAccount     &account,
                                                        uint64_t           nonce,
                                                        const std::string &hold_hash = std::string( 64, 'a' ),
                                                        uint64_t           amount    = 17 )
    {
        SGTransaction::EscrowReleaseTxV2 wire;
        auto                            *dag = wire.mutable_dag_struct();
        dag->set_type( "escrow-release-v2" );
        dag->set_source_addr( account.GetAddress() );
        dag->set_nonce( nonce );
        dag->set_timestamp( 123456 );
        dag->set_previous_hash( "previous" );
        dag->set_uncle_hash( "untrusted-uncle-text" );
        auto *input = wire.mutable_utxo_params()->add_inputs();
        input->set_tx_id_hash( hold_hash );
        input->set_output_index( 0 );
        const auto input_signature = account.Sign( { 1, 2, 3 } );
        input->set_signature( input_signature.data(), input_signature.size() );
        auto *output = wire.mutable_utxo_params()->add_outputs();
        output->set_encrypted_amount( amount );
        output->set_dest_addr( "recipient" );
        output->set_token_id( std::string( TokenID::ByteArray{}.size(), '\x42' ) );
        dag->set_data_hash( crypto::blake2b_256( MessageBytes( wire ) ).toReadableString() );
        const auto signature = account.Sign( MessageBytes( wire ) );
        dag->set_signature( signature.data(), signature.size() );
        return wire;
    }

    class EscrowReleaseV2CodecTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            first_  = GeniusAccount::NewEphemeral( TokenID::FromBytes( { 0 } ) );
            second_ = GeniusAccount::NewEphemeral( TokenID::FromBytes( { 0 } ) );
            ASSERT_NE( first_, nullptr );
            ASSERT_NE( second_, nullptr );
            ASSERT_NE( first_->GetAddress(), second_->GetAddress() );
        }

        std::shared_ptr<GeniusAccount> first_;
        std::shared_ptr<GeniusAccount> second_;
    };

    TEST_F( EscrowReleaseV2CodecTest, RawAndEmbeddedRoundTripPreserveSignedPayment )
    {
        for ( const auto &account : { first_, second_ } )
        {
            const auto wire    = SignedPaymentWire( *account, 9 );
            const auto bytes   = MessageBytes( wire );
            const auto payment = DecodeReleaseV2( bytes );
            ASSERT_NE( payment, nullptr );
            EXPECT_EQ( payment->SerializeByteVector(), bytes );
            EXPECT_EQ( payment->GetType(), "escrow-release-v2" );
            EXPECT_EQ( payment->GetTransactionSpecificPath(), "escrow-release-v2" );
            EXPECT_EQ( payment->GetSrcAddress(), account->GetAddress() );
            EXPECT_EQ( payment->GetHash(), wire.dag_struct().data_hash() );
            EXPECT_EQ( payment->dag_st.SerializeAsString(), wire.dag_struct().SerializeAsString() );
            EXPECT_TRUE( payment->CheckHash() );
            EXPECT_TRUE( payment->CheckSignature() );
            EXPECT_TRUE( payment->HasUTXOParameters() );
            const auto params = payment->GetUTXOParametersOpt();
            if ( !params )
            {
                FAIL() << "V2 must expose its payment parameters";
            }
            ASSERT_EQ( params->first.size(), 1 );
            EXPECT_EQ( params->first.front().txid_hash_.toReadableString(), std::string( 64, 'a' ) );
            EXPECT_EQ( params->first.front().output_idx_, 0 );
            EXPECT_EQ( params->first.front().signature_, account->Sign( { 1, 2, 3 } ) );
            ASSERT_EQ( params->second.size(), 1 );
            EXPECT_EQ( params->second.front().encrypted_amount, 17 );
            EXPECT_EQ( params->second.front().dest_address, "recipient" );
            const auto &token_bytes = wire.utxo_params().outputs( 0 ).token_id();
            EXPECT_EQ( params->second.front().token_id, TokenID::FromBytes( token_bytes.data(), token_bytes.size() ) );
            EXPECT_EQ( payment->GetTopics(),
                       ( std::unordered_set<std::string>{ account->GetAddress(), "recipient" } ) );
            const auto embedded = payment->SerializeToEmbeddedTransaction();
            ASSERT_EQ( embedded.transaction_case(), EmbeddedTransaction::kEscrowReleaseV2 );
            EXPECT_EQ( MessageBytes( embedded.escrow_release_v2() ), bytes );
            const auto restored = DecodeReleaseV2( MessageBytes( embedded.escrow_release_v2() ) );
            ASSERT_NE( restored, nullptr );
            EXPECT_EQ( restored->SerializeByteVector(), bytes );
            EXPECT_EQ( restored->GetSlotID(), payment->GetSlotID() );
            EXPECT_TRUE( restored->CheckSignature() );
        }
    }

    TEST_F( EscrowReleaseV2CodecTest, SameHoldAcrossPeersHasOneSlot )
    {
        const auto first_wire  = SignedPaymentWire( *first_, 1 );
        const auto second_wire = SignedPaymentWire( *second_, 99, std::string( 64, 'a' ), 43 );
        EXPECT_NE( first_wire.dag_struct().data_hash(), second_wire.dag_struct().data_hash() );
        EXPECT_NE( first_wire.dag_struct().signature(), second_wire.dag_struct().signature() );
        const auto first  = DecodeReleaseV2( MessageBytes( first_wire ) );
        const auto second = DecodeReleaseV2( MessageBytes( second_wire ) );
        ASSERT_NE( first, nullptr );
        ASSERT_NE( second, nullptr );
        EXPECT_EQ( first->GetSlotID(), "escrow-release-v2:" + std::string( 64, 'a' ) + ":0" );
        EXPECT_EQ( first->GetSlotID(), second->GetSlotID() );
        EXPECT_TRUE( first->CheckSignature() );
        EXPECT_TRUE( second->CheckSignature() );
    }

    TEST_F( EscrowReleaseV2CodecTest, DifferentHoldHasDifferentSlot )
    {
        const auto first  = DecodeReleaseV2( MessageBytes( SignedPaymentWire( *first_, 1 ) ) );
        const auto second = DecodeReleaseV2(
            MessageBytes( SignedPaymentWire( *second_, 99, std::string( 64, 'b' ) ) ) );
        ASSERT_NE( first, nullptr );
        ASSERT_NE( second, nullptr );
        EXPECT_NE( first->GetSlotID(), second->GetSlotID() );
    }

    TEST_F( EscrowReleaseV2CodecTest, RejectsMalformedPaymentEncoding )
    {
        const auto good = SignedPaymentWire( *first_, 1 );
        ASSERT_NE( DecodeReleaseV2( MessageBytes( good ) ), nullptr );
        EXPECT_EQ( DecodeReleaseV2( { 0xff } ), nullptr );
        auto invalid = good;
        invalid.mutable_dag_struct()->set_type( "transfer" );
        EXPECT_EQ( DecodeReleaseV2( MessageBytes( invalid ) ), nullptr );
        invalid.clear_dag_struct();
        EXPECT_EQ( DecodeReleaseV2( MessageBytes( invalid ) ), nullptr );
        invalid = good;
        invalid.clear_utxo_params();
        EXPECT_EQ( DecodeReleaseV2( MessageBytes( invalid ) ), nullptr );
        invalid = good;
        invalid.mutable_utxo_params()->clear_inputs();
        EXPECT_EQ( DecodeReleaseV2( MessageBytes( invalid ) ), nullptr );
        invalid                                      = good;
        *invalid.mutable_utxo_params()->add_inputs() = good.utxo_params().inputs( 0 );
        EXPECT_EQ( DecodeReleaseV2( MessageBytes( invalid ) ), nullptr );
        invalid = good;
        invalid.mutable_utxo_params()->mutable_inputs( 0 )->set_output_index( 1 );
        EXPECT_EQ( DecodeReleaseV2( MessageBytes( invalid ) ), nullptr );
        invalid = good;
        invalid.mutable_utxo_params()->mutable_inputs( 0 )->set_tx_id_hash( "invalid-hash" );
        EXPECT_EQ( DecodeReleaseV2( MessageBytes( invalid ) ), nullptr );
        for ( const auto size : { 0, 1, 31, 33 } )
        {
            invalid = good;
            invalid.mutable_utxo_params()->mutable_outputs( 0 )->set_token_id( std::string( size, '\0' ) );
            EXPECT_EQ( DecodeReleaseV2( MessageBytes( invalid ) ), nullptr ) << size;
        }
        invalid = good;
        invalid.mutable_utxo_params()->clear_outputs();
        EXPECT_EQ( DecodeReleaseV2( MessageBytes( invalid ) ), nullptr );
        invalid = good;
        invalid.mutable_utxo_params()->mutable_outputs( 0 )->clear_dest_addr();
        EXPECT_EQ( DecodeReleaseV2( MessageBytes( invalid ) ), nullptr );
    }

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
        int field_number = 0;
        for ( const auto &name : legacy_fields )
        {
            const auto *field = legacy->FindFieldByName( name );
            ASSERT_NE( field, nullptr );
            EXPECT_EQ( field->number(), ++field_number );
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
        field_number                             = 0;
        for ( const auto &name : old_cases )
        {
            const auto *field = embedded->FindFieldByName( name );
            ASSERT_NE( field, nullptr );
            EXPECT_EQ( field->number(), ++field_number );
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
