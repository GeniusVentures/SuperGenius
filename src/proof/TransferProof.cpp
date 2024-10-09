/**
 * @file       TransferProof.cpp
 * @brief      
 * @date       2024-09-29
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "TransferProof.hpp"
#include "circuits/TransactionVerifierCircuit.hpp"
#include <nil/crypto3/multiprecision/cpp_int.hpp>
#include <nil/crypto3/algebra/curves/pallas.hpp>

using namespace nil::crypto3::algebra::curves;

namespace sgns
{

    TransferProof::TransferProof( uint64_t balance, uint64_t amount ) :
        IBasicProof( std::string( TransactionCircuit ) ), //
        balance_( std::move( balance ) ),                 //
        amount_( std::move( amount ) )                    //
    {
    }

    std::pair<boost::json::array, boost::json::array> TransferProof::GenerateJsonParameters()
    {
        boost::json::array public_inputs_json_array;
        boost::json::array private_inputs_json_array;

        private_inputs_json_array.push_back( GenerateIntParameter( balance_ ) );
        private_inputs_json_array.push_back( GenerateIntParameter( amount_ ) );
        private_inputs_json_array.push_back( GenerateFieldParameter( balance_ ) );
        private_inputs_json_array.push_back( GenerateFieldParameter( amount_ ) );
        public_inputs_json_array.push_back( GenerateCurveParameter( balance_ ) );
        public_inputs_json_array.push_back( GenerateCurveParameter( amount_ ) );
        public_inputs_json_array.push_back( GenerateCurveParameter( balance_ - amount_ ) );
        public_inputs_json_array.push_back( GenerateCurveParameter( 0 ) ); //GENERATOR
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

    boost::json::object TransferProof::GenerateCurveParameter( uint64_t value )
    {
        typename pallas::template g1_type<coordinates::affine>::value_type generator( generator_X_point,
                                                                                      generator_Y_point );
        boost::json::object                                                commit_obj;

        boost::json::array commit_coordinates;
        auto               commitment = generator * value;

        std::ostringstream X_coord_stream;
        X_coord_stream << std::hex << commitment.X;

        std::ostringstream Y_coord_stream;
        Y_coord_stream << std::hex << commitment.Y;
        // Ensure the hex string has an even number of characters
        std::string x_coord_string = X_coord_stream.str();
        std::string y_coord_string = Y_coord_stream.str();
        if ( x_coord_string.size() % 2 != 0 )
        {
            x_coord_string = "0" + x_coord_string; // Add leading zero if necessary
        }
        if ( x_coord_string.size() % 2 != 0 )
        {
            x_coord_string = "0" + x_coord_string; // Add leading zero if necessary
        }

        commit_coordinates.push_back( boost::json::value( "0x" + x_coord_string ) );
        commit_coordinates.push_back( boost::json::value( "0x" + x_coord_string ) );
        commit_obj["curve"] = commit_coordinates;

        return commit_obj;
    }

}
