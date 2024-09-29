/**
 * @file       TransferProof.cpp
 * @brief      
 * @date       2024-09-29
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "TransferProof.hpp"

#include <boost/json/src.hpp  
//#include <boost/json.hpp  
//#include "GeniusAssigner.hpp"
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
    X_coord_stream << commitment.X;

    std::ostringstream Y_coord_stream;
    Y_coord_stream << commitment.Y;

    commit_coordinates.push_back( boost::json::value( X_coord_stream.str() ) );
    commit_coordinates.push_back( boost::json::value( Y_coord_stream.str() ) );
    commit_obj["curve"] = commit_coordinates;

    return commit_obj;
}

static boost::json::object GenerateFieldParameter( uint64_t value )
{
    boost::json::object obj;
    obj["field"] = value; // Add {"field": value} to the object

    return obj;
}

static boost::json::object GenerateArrayParameter( std::array<uint64_t, 4> values )
{
    boost::json::object obj;
    //obj["field"] = value; // Add {"field": value} to the object

    return obj;
}

namespace sgns
{

    TransferProof::TransferProof( std::string bytecode_path, uint64_t balance, uint64_t amount ) :
        IBasicProof( std::move( bytecode_path ) ), //
        balance_( std::move( balance ) ),                    //
        amount_( std::move( amount ) )                       //
    {
        //assigner_ = std::make_shared<sgns::GeniusAssigner>();
        //prover_   = std::make_shared<sgns::GeniusProver>();
    }

    void TransferProof::GenerateProof()
    {
        boost::json::array public_inputs_json_array;
        boost::json::array private_inputs_json_array;

        private_inputs_json_array.push_back( GenerateFieldParameter( balance_ ) );
        private_inputs_json_array.push_back( GenerateFieldParameter( amount_ ) );
        private_inputs_json_array.push_back( GenerateFieldParameter( base_seed ) );
        private_inputs_json_array.push_back( GenerateFieldParameter( provided_totp ) );

        public_inputs_json_array.push_back( GenerateCurveParameter( balance_, generator_X_point, generator_Y_point ) );
        public_inputs_json_array.push_back( GenerateCurveParameter( amount_, generator_X_point, generator_Y_point ) );
        public_inputs_json_array.push_back(
            GenerateCurveParameter( balance_ - amount_, generator_X_point, generator_Y_point ) );
        public_inputs_json_array.push_back(
            GenerateCurveParameter( 0, generator_X_point, generator_Y_point ) ); //GENERATOR

        public_inputs_json_array.push_back( GenerateArrayParameter( ranges ) );

        //auto hidden_assigner = std::static_pointer_cast<sgns::GeniusAssigner>( assigner_ );
        //auto hidden_prover = std::static_pointer_cast<sgns::GeniusProver>( assigner_ );

        //hidden_assigner->GenerateCircuitAndTable( public_inputs_json_array, private_inputs_json_array, bytecode_path_ );

        //boost::json::value json_value = public_inputs_json_array;

        // Print the JSON structure
        //std::cout << boost::json::serialize( json_value ) << std::endl;
    }

}
