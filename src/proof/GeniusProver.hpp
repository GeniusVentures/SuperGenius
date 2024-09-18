/**
 * @file       GeniusProver.hpp
 * @brief      
 * @date       2024-09-13
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _GENIUS_PROVER_HPP_
#define _GENIUS_PROVER_HPP_

#include <string>

#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>

#include <boost/test/unit_test.hpp> // TODO: remove this. Required only because of an incorrect assert check in zk

#include "GeniusAssigner.hpp"

#include <nil/marshalling/status_type.hpp>
#include <nil/marshalling/field_type.hpp>
#include <nil/marshalling/endianness.hpp>
#include <nil/crypto3/zk/snark/arithmetization/plonk/params.hpp>
#include <nil/crypto3/zk/snark/arithmetization/plonk/constraint_system.hpp>
#include <nil/crypto3/marshalling/zk/types/plonk/constraint_system.hpp>
#include <nil/crypto3/marshalling/zk/types/plonk/assignment_table.hpp>
#include <nil/crypto3/algebra/curves/pallas.hpp>
#include <nil/crypto3/algebra/fields/arithmetic_params/pallas.hpp>

#include <nil/crypto3/marshalling/zk/types/placeholder/common_data.hpp>
#include <nil/crypto3/marshalling/zk/types/placeholder/proof.hpp>
#include <nil/crypto3/math/algorithms/calculate_domain_set.hpp>
#include <nil/crypto3/multiprecision/cpp_int.hpp>
#include <nil/crypto3/zk/snark/systems/plonk/placeholder/detail/placeholder_policy.hpp>
#include <nil/crypto3/zk/snark/systems/plonk/placeholder/params.hpp>
#include <nil/crypto3/zk/snark/systems/plonk/placeholder/preprocessor.hpp>
#include <nil/crypto3/zk/snark/systems/plonk/placeholder/profiling.hpp>
#include <nil/crypto3/zk/snark/systems/plonk/placeholder/proof.hpp>
#include <nil/crypto3/zk/snark/systems/plonk/placeholder/prover.hpp>
#include <nil/crypto3/zk/snark/systems/plonk/placeholder/verifier.hpp>

#include <nil/crypto3/marshalling/zk/types/commitments/lpc.hpp>

#include <nil/crypto3/algebra/curves/pallas.hpp>
#include <nil/crypto3/hash/keccak.hpp>

#include "outcome/outcome.hpp"

using namespace nil;

namespace sgns
{
    class GeniusProver
    {
        using BlueprintFieldType   = typename crypto3::algebra::curves::pallas::base_field_type;
        using HashType             = crypto3::hashes::keccak_1600<256>;
        using ProverEndianess      = nil::marshalling::option::big_endian;
        using ConstraintSystemType = crypto3::zk::snark::plonk_constraint_system<BlueprintFieldType>;
        using TableDescriptionType = crypto3::zk::snark::plonk_table_description<BlueprintFieldType>;
        using PlonkColumn          = crypto3::zk::snark::plonk_column<BlueprintFieldType>;
        using AssignmentTableType  = crypto3::zk::snark::plonk_table<BlueprintFieldType, PlonkColumn>;
        using LpcParams         = crypto3::zk::commitments::list_polynomial_commitment_params<HashType, HashType, 9, 2>;
        using Lpc               = crypto3::zk::commitments::list_polynomial_commitment<BlueprintFieldType, LpcParams>;
        using LpcScheme         = typename crypto3::zk::commitments::lpc_commitment_scheme<Lpc>;
        using FriParams         = typename Lpc::fri_type::params_type;
        using CircuitParams     = crypto3::zk::snark::placeholder_circuit_params<BlueprintFieldType>;
        using PlaceholderParams = crypto3::zk::snark::placeholder_params<CircuitParams, LpcScheme>;
        using PublicPreprocessor =
            crypto3::zk::snark::placeholder_public_preprocessor<BlueprintFieldType, PlaceholderParams>;
        using PublicPreprocessedData = PublicPreprocessor::preprocessed_data_type;
        using PrivatePreprocessor =
            crypto3::zk::snark::placeholder_private_preprocessor<BlueprintFieldType, PlaceholderParams>;
        using PrivatePreprocessedData = PrivatePreprocessor::preprocessed_data_type;
        using ProofSnarkType          = crypto3::zk::snark::placeholder_proof<BlueprintFieldType, PlaceholderParams>;
        using ProofType  = crypto3::marshalling::types::placeholder_proof<nil::marshalling::field_type<ProverEndianess>,
                                                                          ProofSnarkType>;
        using ProverType = crypto3::zk::snark::placeholder_prover<BlueprintFieldType, PlaceholderParams>;
        using PlonkTablePair = std::pair<TableDescriptionType, AssignmentTableType>;

    public:
        GeniusProver( std::size_t component_constant_columns = COMPONENT_CONSTANT_COLUMNS_DEFAULT,
                      std::size_t expand_factor              = EXPAND_FACTOR_DEFAULT ) :
            component_constant_columns_( component_constant_columns ), expand_factor_( expand_factor )
        {
        }

        enum class ProverError
        {
            INVALID_PROOF_GENERATED = 0,
        };

        outcome::result<ProofType> GenerateProof( const std::vector<GeniusAssigner::AssignerOutput> &assigner_outputs,
                                                  const boost::filesystem::path                     &proof_file_ );

        outcome::result<ProofType> GenerateProof( const boost::filesystem::path &circuit_file,
                                                  const boost::filesystem::path &assignment_table_file );

        bool VerifyProof( const ProofSnarkType         &proof,
                          const PublicPreprocessedData &public_data,
                          const TableDescriptionType   &desc,
                          const ConstraintSystemType   &constrains_sys,
                          const LpcScheme              &scheme ) const;

        //bool generate_to_file( bool                           skip_verification,
        //                       const boost::filesystem::path &circuit_file,
        //                       const boost::filesystem::path &assignment_table_file,
        //                       const boost::filesystem::path &proof_file_ )
        //{
        //    if ( !can_write_to_file( proof_file_.string() ) )
        //    {
        //        BOOST_LOG_TRIVIAL( error ) << "Can't write to file " << proof_file_;
        //        return false;
        //    }

        //    prepare_for_operation( circuit_file, assignment_table_file );

        //    BOOST_ASSERT( public_preprocessed_data_ );
        //    BOOST_ASSERT( private_preprocessed_data_ );
        //    BOOST_ASSERT( table_description_ );
        //    BOOST_ASSERT( constraint_system_ );
        //    BOOST_ASSERT( lpc_scheme_ );
        //    BOOST_ASSERT( fri_params_ );

        //    BOOST_LOG_TRIVIAL( info ) << "Generating proof...";
        //    ProofSnarkType proof = ProverType::process( *public_preprocessed_data_,
        //                                                *private_preprocessed_data_,
        //                                                *table_description_,
        //                                                *constraint_system_,
        //                                                *lpc_scheme_ );
        //    BOOST_LOG_TRIVIAL( info ) << "Proof generated";

        //    if ( skip_verification )
        //    {
        //        BOOST_LOG_TRIVIAL( info ) << "Skipping proof verification";
        //    }
        //    else
        //    {
        //        if ( !verify( proof ) )
        //        {
        //            return false;
        //        }
        //    }

        //    BOOST_LOG_TRIVIAL( info ) << "Writing proof to " << proof_file_;
        //    auto filled_placeholder_proof = FillPlaceholderProof( proof, *fri_params_ );
        //    bool res                      = encode_marshalling_to_file( proof_file_, filled_placeholder_proof, true );
        //    if ( res )
        //    {
        //        BOOST_LOG_TRIVIAL( info ) << "Proof written";
        //    }

        //    return res;
        //}

    private:
        constexpr static const std::size_t COMPONENT_CONSTANT_COLUMNS_DEFAULT = 5;
        constexpr static const std::size_t EXPAND_FACTOR_DEFAULT              = 2;

        const std::size_t component_constant_columns_;
        const std::size_t expand_factor_;

        //std::shared_ptr<ConstraintSystemType>    constraint_system_;
        //std::shared_ptr<TableDescriptionType>    table_description_;
        //std::shared_ptr<FriParams>               fri_params_;
        //std::shared_ptr<LpcScheme>               lpc_scheme_;
        //std::shared_ptr<PublicPreprocessedData>  public_preprocessed_data_;
        //std::shared_ptr<PrivatePreprocessedData> private_preprocessed_data_;

        ConstraintSystemType MakePlonkConstraintSystem( const GeniusAssigner::PlonkConstraintSystemType &constrains );

        PlonkTablePair MakePlonkTableDescription( const GeniusAssigner::PlonkAssignTableType &table );

        FriParams MakeFRIParams( std::size_t degree_log, const int max_step = 1, std::size_t expand_factor = 0 );


        //bool prepare_for_operation( const boost::filesystem::path &circuit_file,
        //                            const boost::filesystem::path &assignment_table_file )
        //{
        //    using TTypeBase             = nil::marshalling::field_type<ProverEndianess>;
        //    using ConstraintMarshalling = //HENRIQUE
        //        crypto3::marshalling::types::plonk_constraint_system<TTypeBase, ConstraintSystemType>;
        //
        //    using Column          = crypto3::zk::snark::plonk_column<BlueprintFieldType>;
        //    using AssignmentTable = crypto3::zk::snark::plonk_table<BlueprintFieldType, Column>;
        //
        //    {
        //        auto marshalled_value = decode_marshalling_from_file<ConstraintMarshalling>( circuit_file );
        //        if ( !marshalled_value )
        //        {
        //            return false;
        //        }
        //        constraint_system_ = std::make_shared<ConstraintSystemType>(
        //            crypto3::marshalling::types::make_plonk_constraint_system<ProverEndianess, ConstraintSystemType>(
        //                *marshalled_value ) );
        //    }
        //    //henrique
        //    using TableValueMarshalling =
        //        crypto3::marshalling::types::plonk_assignment_table<TTypeBase, AssignmentTable>;
        //    auto marshalled_table = decode_marshalling_from_file<TableValueMarshalling>( assignment_table_file );
        //    if ( !marshalled_table )
        //    {
        //        return false;
        //    }
        //    auto [table_description, assignment_table] =
        //        nil::crypto3::marshalling::types::make_assignment_table<ProverEndianess, AssignmentTable>(
        //            *marshalled_table );
        //    table_description_ = std::make_shared<TableDescriptionType>( table_description );
        //
        //    // Lambdas and grinding bits should be passed threw preprocessor directives
        //    std::size_t table_rows_log = std::ceil( std::log2( table_description_->rows_amount ) );
        //
        //    fri_params_ = std::make_shared<FriParams>(
        //        create_fri_params<typename Lpc::fri_type, BlueprintFieldType>( table_rows_log, 1, expand_factor_ ) );
        //
        //    std::size_t permutation_size = table_description_->witness_columns +
        //                                   table_description_->public_input_columns + component_constant_columns_;
        //    lpc_scheme_ = std::make_shared<LpcScheme>( *fri_params_ );
        //
        //    public_preprocessed_data_ = std::make_shared<PublicPreprocessedData>(
        //        crypto3::zk::snark::placeholder_public_preprocessor<BlueprintFieldType, PlaceholderParams>::process(
        //            *constraint_system_,
        //            assignment_table.move_public_table(),
        //            *table_description_,
        //            *lpc_scheme_,
        //            permutation_size ) );
        //
        //    BOOST_LOG_TRIVIAL( info ) << "Preprocessing private data";
        //    private_preprocessed_data_ = std::make_shared<PrivatePreprocessedData>(
        //        nil::crypto3::zk::snark::placeholder_private_preprocessor<BlueprintFieldType, PlaceholderParams>::
        //            process( *constraint_system_, assignment_table.move_private_table(), *table_description_ ) );
        //    return true;
        //}

        template <typename MarshallingType>
        std::optional<MarshallingType> decode_marshalling_from_file( const boost::filesystem::path &path,
                                                                     bool                           hex = false )
        {
            const auto v = hex ? read_hex_file_to_vector( path.c_str() ) : read_file_to_vector( path.c_str() );
            if ( !v.has_value() )
            {
                return std::nullopt;
            }

            MarshallingType marshalled_data;
            auto            read_iter = v->begin();
            auto            status    = marshalled_data.read( read_iter, v->size() );
            if ( status != nil::marshalling::status_type::success )
            {
                BOOST_LOG_TRIVIAL( error ) << "Marshalled structure decoding failed";
                return std::nullopt;
            }
            return marshalled_data;
        }

        // HEX data format is not efficient, we will remove it later
        std::optional<std::vector<std::uint8_t>> read_hex_file_to_vector( const std::string &path )
        {
            auto file = open_file<std::ifstream>( path, std::ios_base::in );
            if ( !file.has_value() )
            {
                return std::nullopt;
            }

            std::ifstream       &stream = file.value();
            std::vector<uint8_t> result;
            std::string          line;
            while ( std::getline( stream, line ) )
            {
                if ( line.rfind( "0x", 0 ) == 0 && line.length() >= 3 )
                {
                    for ( size_t i = 2; i < line.length(); i += 2 )
                    {
                        std::string hex_string = line.substr( i, 2 );
                        uint8_t     byte       = static_cast<uint8_t>( std::stoul( hex_string, nullptr, 16 ) );
                        result.push_back( byte );
                    }
                }
                else
                {
                    BOOST_LOG_TRIVIAL( error ) << "File contains non-hex string";
                    return std::nullopt;
                }
            }

            return result;
        }

        std::optional<std::vector<std::uint8_t>> read_file_to_vector( const std::string &path )
        {
            auto file = open_file<std::ifstream>( path, std::ios_base::in | std::ios::binary | std::ios::ate );
            if ( !file.has_value() )
            {
                return std::nullopt;
            }

            std::ifstream  &stream = file.value();
            std::streamsize fsize  = stream.tellg();
            stream.seekg( 0, std::ios::beg );
            std::vector<std::uint8_t> v( static_cast<size_t>( fsize ) );

            stream.read( reinterpret_cast<char *>( v.data() ), fsize );

            if ( stream.fail() )
            {
                BOOST_LOG_TRIVIAL( error ) << "Error occurred during reading file " << path;
                return std::nullopt;
            }

            return v;
        }

        std::vector<std::size_t> GenerateRandomStepList( const std::size_t r, const int max_step );

        template <typename StreamType>
        std::optional<StreamType> open_file( const std::string &path, std::ios_base::openmode mode )
        {
            StreamType file( path, mode );
            if ( !file.is_open() )
            {
                BOOST_LOG_TRIVIAL( error ) << "Unable to open file: " << path;
                return std::nullopt;
            }

            return file;
        }

        inline bool can_write_to_file( const std::string &path )
        {
            if ( !is_valid_path( path ) )
            {
                return false;
            }

            if ( boost::filesystem::exists( path ) )
            {
                std::ofstream file( path, std::ios::out | std::ios::app );
                bool          can_write = file.is_open();
                return can_write;
            }
            else
            {
                boost::filesystem::path boost_path = boost::filesystem::absolute( path );
                boost::filesystem::path parent_dir = boost_path.parent_path();
                if ( parent_dir.empty() )
                {
                    BOOST_LOG_TRIVIAL( error ) << "Proof parent dir is empty. Seems like you "
                                                  "are passing an empty string.";
                    return false;
                }
                if ( !boost::filesystem::exists( parent_dir ) )
                {
                    BOOST_LOG_TRIVIAL( error ) << boost_path << ": proof parent dir does not exist. Create it first.";
                    return false;
                }
                std::string   temp_file_name = ( parent_dir / "temp_file_to_test_write_permission" ).string();
                std::ofstream temp_file( temp_file_name, std::ios::out );
                bool          can_write = temp_file.is_open();
                temp_file.close();

                if ( can_write )
                {
                    boost::filesystem::remove( temp_file_name );
                }
                return can_write;
            }
        }

        inline bool is_valid_path( const std::string &path )
        {
            if ( path.length() >= PATH_MAX )
            {
                BOOST_LOG_TRIVIAL( error )
                    << path << ": file path is too long. Maximum allowed length is " << PATH_MAX << " characters.";
                return false;
            }

            if ( boost::filesystem::path( path ).filename().string().length() >= FILENAME_MAX )
            {
                BOOST_LOG_TRIVIAL( error )
                    << path << ": file name is too long. Maximum allowed length is " << FILENAME_MAX << " characters.";
                return false;
            }
            return true;
        }

        ProofType FillPlaceholderProof( const ProofSnarkType &proof, const FriParams &commitment_params );

        template <typename MarshallingType>
        bool encode_marshalling_to_file( const boost::filesystem::path &path,
                                         const MarshallingType         &data_for_marshalling,
                                         bool                           hex = false )
        {
            std::vector<std::uint8_t> v;
            v.resize( data_for_marshalling.length(), 0x00 );
            auto                          write_iter = v.begin();
            nil::marshalling::status_type status     = data_for_marshalling.write( write_iter, v.size() );
            if ( status != nil::marshalling::status_type::success )
            {
                BOOST_LOG_TRIVIAL( error ) << "Marshalled structure encoding failed";
                return false;
            }

            return hex ? write_vector_to_hex_file( v, path.c_str() ) : write_vector_to_file( v, path.c_str() );
        }

        bool write_vector_to_hex_file( const std::vector<std::uint8_t> &vector, const std::string &path )
        {
            auto file = open_file<std::ofstream>( path, std::ios_base::out );
            if ( !file.has_value() )
            {
                return false;
            }

            std::ofstream &stream = file.value();

            stream << "0x" << std::hex;
            for ( auto it = vector.cbegin(); it != vector.cend(); ++it )
            {
                stream << std::setfill( '0' ) << std::setw( 2 ) << std::right << int( *it );
            }
            stream << std::dec;

            if ( stream.fail() )
            {
                BOOST_LOG_TRIVIAL( error ) << "Error occurred during writing to file " << path;
                return false;
            }

            return true;
        }

        bool write_vector_to_file( const std::vector<std::uint8_t> &vector, const std::string &path )
        {
            auto file = open_file<std::ofstream>( path, std::ios_base::out | std::ios_base::binary );
            if ( !file.has_value() )
            {
                return false;
            }

            std::ofstream &stream = file.value();
            stream.write( reinterpret_cast<const char *>( vector.data() ), vector.size() );

            if ( stream.fail() )
            {
                BOOST_LOG_TRIVIAL( error ) << "Error occured during writing file " << path;
                return false;
            }

            return true;
        }
    };
}

OUTCOME_HPP_DECLARE_ERROR_2( sgns, GeniusProver::ProverError );

#endif
