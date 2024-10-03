/**
 * @file       TransferProof.cpp
 * @brief      
 * @date       2024-09-29
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "TransferProof.hpp"

#include <boost/json.hpp>
#include "GeniusAssigner.hpp"
#include "GeniusProver.hpp"
#include <nil/crypto3/multiprecision/cpp_int.hpp>
#include <nil/crypto3/algebra/curves/pallas.hpp>

#include <nil/crypto3/algebra/marshalling.hpp>
using namespace nil::crypto3::algebra::curves;

static boost::json::object GenerateCurveParameter( uint64_t value, uint64_t X_generator, uint64_t Y_generator )
{
    typename pallas::template g1_type<coordinates::affine>::value_type generator( X_generator, Y_generator );
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

static boost::json::object GenerateFieldParameter( uint64_t value )
{
    boost::json::object obj;
    //std::ostringstream  oss;
    //oss << std::hex << value;
    //obj["field"] = "0x" + oss.str(); // Add {"field": value} to the object
    obj["field"] = value; // Add {"field": value} to the object

    return obj;
}

static boost::json::object GenerateArrayParameter( std::array<uint64_t, 4> values )
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
static boost::json::object GenerateIntParameter( uint64_t value )
{
    boost::json::object obj;
    obj["int"] = value; 

    return obj;
}

namespace sgns
{

    TransferProof::TransferProof( std::string bytecode_path, uint64_t balance, uint64_t amount ) :
        IBasicProof( std::move( bytecode_path ) ), //
        balance_( std::move( balance ) ),          //
        amount_( std::move( amount ) )             //
    {
        assigner_ = std::make_shared<sgns::GeniusAssigner>();
        prover_   = std::make_shared<sgns::GeniusProver>();
    }

    std::pair<boost::json::array, boost::json::array> TransferProof::GenerateJsonParameters()
    {
        boost::json::array public_inputs_json_array;
        boost::json::array private_inputs_json_array;

        private_inputs_json_array.push_back( GenerateIntParameter( balance_ ) );
        private_inputs_json_array.push_back( GenerateIntParameter( amount_ ) );
        private_inputs_json_array.push_back( GenerateFieldParameter( balance_ ) );
        private_inputs_json_array.push_back( GenerateFieldParameter( amount_ ) );
        public_inputs_json_array.push_back( GenerateCurveParameter( balance_, generator_X_point, generator_Y_point ) );
        public_inputs_json_array.push_back( GenerateCurveParameter( amount_, generator_X_point, generator_Y_point ) );
        public_inputs_json_array.push_back(
            GenerateCurveParameter( balance_ - amount_, generator_X_point, generator_Y_point ) );
        public_inputs_json_array.push_back(
            GenerateCurveParameter( 0, generator_X_point, generator_Y_point ) ); //GENERATOR
        public_inputs_json_array.push_back( GenerateArrayParameter( ranges ) );
        private_inputs_json_array.push_back( GenerateFieldParameter( base_seed ) );
        private_inputs_json_array.push_back( GenerateFieldParameter( provided_totp ) );



        std::cout << "The path is " << bytecode_path_ << std::endl;

        boost::json::value json_value = public_inputs_json_array;

        // Print the JSON structure
        std::cout << boost::json::serialize( json_value ) << std::endl;

        std::cout << "Private params " << std::endl;
        boost::json::value json_value2 = private_inputs_json_array;

        // Print the JSON structure
        std::cout << boost::json::serialize( json_value2 ) << std::endl;
        return std::make_pair( public_inputs_json_array, private_inputs_json_array );
    }

    void TransferProof::GenerateProof()
    {
        auto [public_inputs_json_array, private_inputs_json_array] = GenerateJsonParameters();

        auto hidden_assigner = std::static_pointer_cast<sgns::GeniusAssigner>( assigner_ );
        auto hidden_prover   = std::static_pointer_cast<sgns::GeniusProver>( prover_ );

        auto assign_result = hidden_assigner->GenerateCircuitAndTable( public_inputs_json_array,
                                                                       private_inputs_json_array,
                                                                       bytecode_path_ );
        std::cout << "After assign gen" << std::endl;
        if ( assign_result.has_value() )
        {
            auto assign_value = assign_result.value().at( 0 );
            auto proof_result = hidden_prover->GenerateProof( assign_value );
            std::cout << "After proof gen" << std::endl;
            if ( proof_result.has_value() )
            {
                if ( hidden_prover->VerifyProof( proof_result.value(), assign_value ) )
                {
                    std::cout << "Proof is valid" << std::endl;
                    hidden_prover->WriteProofToFile(
                        proof_result.value(),
                        "/home/henrique/gnus/SuperGenius/src/proof/transaction_proof.bin" );
                }
            }
        }
        else
        {
            std::cout << "No assignment" << std::endl;
        }
        //boost::json::value json_value = public_inputs_json_array;

        // Print the JSON structure
        //std::cout << boost::json::serialize( json_value ) << std::endl;
    }

}
