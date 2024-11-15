/**
 * @file       TransferProof.cpp
 * @brief      
 * @date       2024-09-29
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "TransferProof.hpp"
#include <nil/crypto3/multiprecision/cpp_int.hpp>
#include <nil/crypto3/algebra/curves/pallas.hpp>
#include <nil/crypto3/hash/algorithm/hash.hpp>
#include <nil/crypto3/hash/adaptor/hashed.hpp>
#include <nil/crypto3/algebra/marshalling.hpp>
#include "proof/proto/SGProof.pb.h"

using namespace nil::crypto3::algebra::curves;

namespace sgns
{

    TransferProof::TransferProof( uint64_t balance, uint64_t amount ) :
#ifdef RELEASE_BYTECODE_CIRCUITS
        IBasicProof( std::string( TransactionCircuit ) ), //
#else
        IBasicProof( std::string( TransactionCircuitDebug ) ), //
#endif
        balance_( std::move( balance ) ), //
        amount_( std::move( amount ) )    //
    {
    }

    template <>
    boost::json::object TransferProof::GenerateCurveParameter(
        pallas::template g1_type<coordinates::affine>::value_type value )
    {
        boost::json::object commit_obj;

        boost::json::array commit_coordinates;

        std::ostringstream X_coord_stream;
        X_coord_stream << std::hex << value.X;

        std::ostringstream Y_coord_stream;
        Y_coord_stream << std::hex << value.Y;

        std::string x_coord_string = X_coord_stream.str();
        std::string y_coord_string = Y_coord_stream.str();
        if ( x_coord_string.size() % 2 != 0 )
        {
            x_coord_string = "0" + x_coord_string;
        }
        if ( y_coord_string.size() % 2 != 0 )
        {
            y_coord_string = "0" + y_coord_string;
        }

        commit_coordinates.push_back( boost::json::value( "0x" + x_coord_string ) );
        commit_coordinates.push_back( boost::json::value( "0x" + y_coord_string ) );
        commit_obj["curve"] = commit_coordinates;

        return commit_obj;
    }

    outcome::result<std::vector<uint8_t>> TransferProof::SerializeFullProof( const SGProof::BaseProofData &base_proof )
    {
        SGProof::TransferProofProto transfer_proof_proto;
        transfer_proof_proto.mutable_proof_data()->CopyFrom( base_proof );
        typename pallas::template g1_type<coordinates::affine>::value_type generator( generator_X_point,
                                                                                      generator_Y_point );
        auto                                                               balance_commitment = balance_ * generator;
        auto                                                               amount_commitment  = amount_ * generator;
        auto new_balance_commitment = ( balance_ - amount_ ) * generator;

        auto PointToVector =
            []( typename pallas::template g1_type<coordinates::affine>::value_type point ) -> std::vector<std::uint8_t>
        {
            std::vector<std::uint8_t> retval( 64 );

            nil::marshalling::bincode::field<pallas::base_field_type>::field_element_to_bytes<
                std::vector<std::uint8_t>::iterator>( point.X, retval.begin(), retval.begin() + retval.size() / 2 );
            nil::marshalling::bincode::field<pallas::base_field_type>::field_element_to_bytes<
                std::vector<std::uint8_t>::iterator>( point.Y, retval.begin() + retval.size() / 2, retval.end() );
            return retval;
        };

        auto balance_vector     = PointToVector( balance_commitment );
        auto amount_vector      = PointToVector( amount_commitment );
        auto new_balance_vector = PointToVector( new_balance_commitment );
        auto generator_vector   = PointToVector( generator );

        transfer_proof_proto.mutable_public_input()->set_balance_commitment(
            std::string( balance_vector.begin(), balance_vector.end() ) );
        transfer_proof_proto.mutable_public_input()->set_amount_commitment(
            std::string( amount_vector.begin(), amount_vector.end() ) );
        transfer_proof_proto.mutable_public_input()->set_new_balance_commitment(
            std::string( new_balance_vector.begin(), new_balance_vector.end() ) );
        transfer_proof_proto.mutable_public_input()->set_generator(
            std::string( generator_vector.begin(), generator_vector.end() ) );
        for ( const auto &value : ranges )
        {
            transfer_proof_proto.mutable_public_input()->add_ranges( value );
        }

        size_t               size = transfer_proof_proto.ByteSizeLong();
        std::vector<uint8_t> serialized_proto( size );

        transfer_proof_proto.SerializeToArray( serialized_proto.data(), serialized_proto.size() );
        return serialized_proto;
    }

    std::pair<boost::json::array, boost::json::array> TransferProof::GenerateJsonParameters()
    {
        boost::json::array                                                 public_inputs_json_array;
        boost::json::array                                                 private_inputs_json_array;
        typename pallas::template g1_type<coordinates::affine>::value_type generator( generator_X_point,
                                                                                      generator_Y_point );

        private_inputs_json_array.push_back( GenerateIntParameter( balance_ ) );
        private_inputs_json_array.push_back( GenerateIntParameter( amount_ ) );
        private_inputs_json_array.push_back( GenerateFieldParameter( balance_ ) );
        private_inputs_json_array.push_back( GenerateFieldParameter( amount_ ) );
        public_inputs_json_array.push_back( GenerateCurveParameter( balance_ * generator ) );
        public_inputs_json_array.push_back( GenerateCurveParameter( amount_ * generator ) );
        public_inputs_json_array.push_back( GenerateCurveParameter( ( balance_ - amount_ ) * generator ) );
        public_inputs_json_array.push_back( GenerateCurveParameter( generator ) ); //GENERATOR
        public_inputs_json_array.push_back( GenerateArrayParameter( ranges ) );
        private_inputs_json_array.push_back( GenerateFieldParameter( base_seed ) );
        private_inputs_json_array.push_back( GenerateFieldParameter( provided_totp ) );

        //boost::json::value json_value = public_inputs_json_array;

        //// Print the JSON structure
        //std::cout << boost::json::serialize( json_value ) << std::endl;

        //boost::json::value json_value2 = private_inputs_json_array;

        //// Print the JSON structure
        //std::cout << boost::json::serialize( json_value2 ) << std::endl;

        return std::make_pair( public_inputs_json_array, private_inputs_json_array );
    }

    outcome::result<std::pair<boost::json::array, boost::json::array>> TransferProof::DeSerializePublicParams(
        const std::vector<uint8_t> &full_proof_data )
    {
        SGProof::TransferProofProto transfer_proof; //proof_data
        if ( !transfer_proof.ParseFromArray( full_proof_data.data(), full_proof_data.size() ) )
        {
            return IBasicProof::Error::INVALID_PROTO_PROOF;
        }
        if ( transfer_proof.proof_data().type() != std::string( TRANSFER_TYPE_NAME ) )
        {
            return IBasicProof::Error::UNEXPECTED_PROOF_TYPE;
        }
        auto StringToVector = []( const std::string &bytes ) -> std::vector<uint8_t>
        { return std::vector<uint8_t>( bytes.begin(), bytes.end() ); };

        auto VectorToPoint = []( std::vector<uint8_t> bytes ) ->
            typename pallas::template g1_type<coordinates::affine>::value_type
        {
            auto x_data =
                nil::marshalling::bincode::field<typename pallas::base_field_type>::template field_element_from_bytes<
                    std::vector<std::uint8_t>::iterator>( bytes.begin(), bytes.begin() + bytes.size() / 2 );
            auto y_data =
                nil::marshalling::bincode::field<typename pallas::base_field_type>::template field_element_from_bytes<
                    std::vector<std::uint8_t>::iterator>( bytes.begin() + bytes.size() / 2, bytes.end() );
            typename pallas::template g1_type<coordinates::affine>::value_type point( x_data.second, y_data.second );
            return point;
        };

        std::vector<uint8_t> balance_commitment_data =
            std::vector<uint8_t>( StringToVector( transfer_proof.public_input().balance_commitment() ) );
        std::vector<uint8_t> amount_commitment_data =
            std::vector<uint8_t>( StringToVector( transfer_proof.public_input().amount_commitment() ) );
        std::vector<uint8_t> new_balance_commitment_data =
            std::vector<uint8_t>( StringToVector( transfer_proof.public_input().new_balance_commitment() ) );
        std::vector<uint8_t> generator_data =
            std::vector<uint8_t>( StringToVector( transfer_proof.public_input().generator() ) );

        auto balance_commitment     = VectorToPoint( balance_commitment_data );
        auto amount_commitment      = VectorToPoint( amount_commitment_data );
        auto new_balance_commitment = VectorToPoint( new_balance_commitment_data );
        auto generator              = VectorToPoint( generator_data );

        if ( transfer_proof.public_input().ranges_size() != 4 )
        {
            return IBasicProof::Error::INVALID_PUBLIC_PARAMETERS;
        }

        std::array<uint64_t, 4> ranges;
        for ( auto i = 0; i < transfer_proof.public_input().ranges_size(); ++i )
        {
            ranges[i] = transfer_proof.public_input().ranges( i );
        }
        boost::json::array public_inputs_json_array;
        boost::json::array private_inputs_json_array;

        private_inputs_json_array.push_back( TransferProof::GenerateIntParameter( 0 ) );
        private_inputs_json_array.push_back( TransferProof::GenerateIntParameter( 0 ) );
        private_inputs_json_array.push_back( TransferProof::GenerateFieldParameter( 0 ) );
        private_inputs_json_array.push_back( TransferProof::GenerateFieldParameter( 0 ) );
        public_inputs_json_array.push_back( TransferProof::GenerateCurveParameter( balance_commitment ) );
        public_inputs_json_array.push_back( TransferProof::GenerateCurveParameter( amount_commitment ) );
        public_inputs_json_array.push_back( TransferProof::GenerateCurveParameter( new_balance_commitment ) );
        public_inputs_json_array.push_back( TransferProof::GenerateCurveParameter( generator ) );
        public_inputs_json_array.push_back( TransferProof::GenerateArrayParameter( ranges ) );
        private_inputs_json_array.push_back( TransferProof::GenerateFieldParameter( 0 ) );
        private_inputs_json_array.push_back( TransferProof::GenerateFieldParameter( 0 ) );

        //boost::json::value json_value = public_inputs_json_array;
        //
        //// Print the JSON structure
        //std::cout << boost::json::serialize( json_value ) << std::endl;
        //
        //boost::json::value json_value2 = private_inputs_json_array;
        //
        //// Print the JSON structure
        //std::cout << boost::json::serialize( json_value2 ) << std::endl;
        return std::make_pair( public_inputs_json_array, private_inputs_json_array );
    }

    boost::json::object TransferProof::GenerateIntParameter( uint64_t value )
    {
        boost::json::object obj;
        obj["int"] = value;

        return obj;
    }

    boost::json::object TransferProof::GenerateArrayParameter( std::array<uint64_t, 4> values )
    {
        boost::json::array field_array;

        for ( const auto &value : values )
        {
            field_array.push_back( GenerateFieldParameter( value ) );
        }
        boost::json::object array_obj;
        array_obj["array"] = field_array;

        return array_obj;
    }

    boost::json::object TransferProof::GenerateFieldParameter( uint64_t value )
    {
        boost::json::object obj;
        obj["field"] = value;

        return obj;
    }

}
