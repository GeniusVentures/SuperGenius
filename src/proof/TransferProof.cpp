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
        IBasicProof( std::string( TransactionCircuit ) ), //
        balance_( std::move( balance ) ),                 //
        amount_( std::move( amount ) )                    //
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
        // Ensure the hex string has an even number of characters
        std::string x_coord_string = X_coord_stream.str();
        std::string y_coord_string = Y_coord_stream.str();
        if ( x_coord_string.size() % 2 != 0 )
        {
            x_coord_string = "0" + x_coord_string; // Add leading zero if necessary
        }
        if ( y_coord_string.size() % 2 != 0 )
        {
            y_coord_string = "0" + y_coord_string; // Add leading zero if necessary
        }

        commit_coordinates.push_back( boost::json::value( "0x" + x_coord_string ) );
        commit_coordinates.push_back( boost::json::value( "0x" + y_coord_string ) );
        commit_obj["curve"] = commit_coordinates;

        return commit_obj;
    }

    outcome::result<std::vector<uint8_t>> TransferProof::SerializePublicParameters()
    {
        typename pallas::template g1_type<coordinates::affine>::value_type generator( generator_X_point,
                                                                                      generator_Y_point );
        SGProof::TransferProofPublicInputs                                 pub_inputs;
        auto                                                               balance_commitment = balance_ * generator;
        auto                                                               amount_commitment  = amount_ * generator;
        auto new_balance_commitment = ( balance_ - amount_ ) * generator;

        std::vector<std::uint8_t> balance_vector( 64 );

        nil::marshalling::bincode::field<pallas::base_field_type>::field_element_to_bytes<
            std::vector<std::uint8_t>::iterator>( balance_commitment.X,
                                                  balance_vector.begin(),
                                                  balance_vector.begin() + balance_vector.size() / 2 );
        nil::marshalling::bincode::field<pallas::base_field_type>::field_element_to_bytes<
            std::vector<std::uint8_t>::iterator>( balance_commitment.Y,
                                                  balance_vector.begin() + balance_vector.size() / 2,
                                                  balance_vector.end() );

        std::vector<std::uint8_t> amount_vector( 64 );

        nil::marshalling::bincode::field<pallas::base_field_type>::field_element_to_bytes<
            std::vector<std::uint8_t>::iterator>( amount_commitment.X,
                                                  amount_vector.begin(),
                                                  amount_vector.begin() + amount_vector.size() / 2 );
        nil::marshalling::bincode::field<pallas::base_field_type>::field_element_to_bytes<
            std::vector<std::uint8_t>::iterator>( amount_commitment.Y,
                                                  amount_vector.begin() + amount_vector.size() / 2,
                                                  amount_vector.end() );
        std::vector<std::uint8_t> new_balance_vector( 64 );

        nil::marshalling::bincode::field<pallas::base_field_type>::field_element_to_bytes<
            std::vector<std::uint8_t>::iterator>( new_balance_commitment.X,
                                                  new_balance_vector.begin(),
                                                  new_balance_vector.begin() + new_balance_vector.size() / 2 );
        nil::marshalling::bincode::field<pallas::base_field_type>::field_element_to_bytes<
            std::vector<std::uint8_t>::iterator>( new_balance_commitment.Y,
                                                  new_balance_vector.begin() + new_balance_vector.size() / 2,
                                                  new_balance_vector.end() );
        std::vector<std::uint8_t> generator_vector( 64 );

        nil::marshalling::bincode::field<pallas::base_field_type>::field_element_to_bytes<
            std::vector<std::uint8_t>::iterator>( generator.X,
                                                  generator_vector.begin(),
                                                  generator_vector.begin() + generator_vector.size() / 2 );
        nil::marshalling::bincode::field<pallas::base_field_type>::field_element_to_bytes<
            std::vector<std::uint8_t>::iterator>( generator.Y,
                                                  generator_vector.begin() + generator_vector.size() / 2,
                                                  generator_vector.end() );

        pub_inputs.set_balance_commitment( std::string( balance_vector.begin(), balance_vector.end() ) );
        pub_inputs.set_amount_commitment( std::string( amount_vector.begin(), amount_vector.end() ) );
        pub_inputs.set_new_balance_commitment( std::string( new_balance_vector.begin(), new_balance_vector.end() ) );
        pub_inputs.set_generator( std::string( generator_vector.begin(), generator_vector.end() ) );
        //TODO set ranges once we use them
        //pub_inputs.set_ranges(  );

        size_t               size = pub_inputs.ByteSizeLong();
        std::vector<uint8_t> serialized_proto( size );

        pub_inputs.SerializeToArray( serialized_proto.data(), serialized_proto.size() );
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

        boost::json::value json_value = public_inputs_json_array;

        // Print the JSON structure
        //std::cout << boost::json::serialize( json_value ) << std::endl;

        boost::json::value json_value2 = private_inputs_json_array;

        // Print the JSON structure
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
        if ( transfer_proof.proof_data().type() != "Transfer" )
        {
            return IBasicProof::Error::UNEXPECTED_PROOF_TYPE;
        }
        auto bytesToVector = []( const std::string &bytes ) -> std::vector<uint8_t>
        { return std::vector<uint8_t>( bytes.begin(), bytes.end() ); };

        std::vector<uint8_t> balance_commitment_data =
            std::vector<uint8_t>( bytesToVector( transfer_proof.public_input().balance_commitment() ) );
        std::vector<uint8_t> amount_commitment_data =
            std::vector<uint8_t>( bytesToVector( transfer_proof.public_input().amount_commitment() ) );
        std::vector<uint8_t> new_balance_commitment_data =
            std::vector<uint8_t>( bytesToVector( transfer_proof.public_input().new_balance_commitment() ) );
        std::vector<uint8_t> generator_data =
            std::vector<uint8_t>( bytesToVector( transfer_proof.public_input().generator() ) );

        auto x_data =
            nil::marshalling::bincode::field<typename pallas::base_field_type>::template field_element_from_bytes<
                std::vector<std::uint8_t>::iterator>( balance_commitment_data.begin(),
                                                      balance_commitment_data.begin() +
                                                          balance_commitment_data.size() / 2 );
        auto y_data =
            nil::marshalling::bincode::field<typename pallas::base_field_type>::template field_element_from_bytes<
                std::vector<std::uint8_t>::iterator>( balance_commitment_data.begin() +
                                                          balance_commitment_data.size() / 2,
                                                      balance_commitment_data.end() );
        typename pallas::template g1_type<coordinates::affine>::value_type balance_commitment( x_data.second,
                                                                                               y_data.second );
        std::array<uint64_t, 4>                                            ranges;
        for ( auto i = 0; ( i < transfer_proof.public_input().ranges_size() ) && ( i < ranges.size() ); ++i )
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
        public_inputs_json_array.push_back( TransferProof::GenerateCurveParameter( balance_commitment ) );
        public_inputs_json_array.push_back(
            TransferProof::GenerateCurveParameter( balance_commitment - balance_commitment ) );
        public_inputs_json_array.push_back( TransferProof::GenerateCurveParameter( balance_commitment ) ); //GENERATOR
        public_inputs_json_array.push_back( TransferProof::GenerateArrayParameter( ranges ) );
        private_inputs_json_array.push_back( TransferProof::GenerateFieldParameter( 0 ) );
        private_inputs_json_array.push_back( TransferProof::GenerateFieldParameter( 0 ) );

        boost::json::value json_value = public_inputs_json_array;

        // Print the JSON structure
        //std::cout << boost::json::serialize( json_value ) << std::endl;

        boost::json::value json_value2 = private_inputs_json_array;

        // Print the JSON structure
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
            field_array.push_back( GenerateFieldParameter( value ) ); // Add to the array
        }
        boost::json::object array_obj;
        array_obj["array"] = field_array;

        return array_obj;
    }

    boost::json::object TransferProof::GenerateFieldParameter( uint64_t value )
    {
        boost::json::object obj;
        obj["field"] = value; // Add {"field": value} to the object

        return obj;
    }

   // bool TransferProof::Register()


}
