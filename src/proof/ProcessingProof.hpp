/**
 * @file       ProcessingProof.hpp
 * @brief      Derived class for generating and verifying processing proofs
 * @date       2024-09-29
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _PROCESSING_PROOF_HPP_
#define _PROCESSING_PROOF_HPP_

#include <string>
#include <cstdint>
#include <array>
#include <utility>
#include <boost/json.hpp>
#include <string_view>
#include "IBasicProof.hpp"

namespace sgns
{
    /**
     * @class       ProcessingProof
     * @brief       A class for generating and verifying processing proofs.
     *
     *              ProcessingProof is a derived class from IBasicProof, providing the specific
     *              implementation for handling processing of funds proof generation and verification.
     */
    class ProcessingProof : public IBasicProof
    {
    public:
        /**
         * @brief       Constructor for the ProcessingProof class.
         * @param[in]   balance The balance of the account for the processing.
         * @param[in]   amount The amount to be processingred.
         */
        explicit ProcessingProof( std::string subtask_id ) :
            IBasicProof( "Bypass" ), //
            subtask_id_m( std::move( subtask_id ) )
        {
        }

        /**
         * @brief       Destructor for the ProcessingProof class.
         */
        ~ProcessingProof() = default;

        /**
         * @brief       Retrieves the type of proof for processings.
         * @return      A string representing the type of the proof.
         */
        std::string GetProofType() const override
        {
            return std::string( PROCESSING_TYPE_NAME );
        }

    private:
        std::string subtask_id_m; ///< The balance associated with the processing.

        /**
         * @brief       Serializes the full proof data and parameters
         * @param[in]   base_proof_data The base proof data to be appended to the public parameters
         * @return      A result containing the serialized full proof in bytes.
         */
        outcome::result<std::vector<uint8_t>> SerializeFullProof(
            const SGProof::BaseProofData &base_proof_data ) override;

        /**
         * @brief       Generates the parameters in JSON array form.
         * @return      A pair of public and private JSON arrays, respectively.
         */
        std::pair<boost::json::array, boost::json::array> GenerateJsonParameters() override;

        /**
         * @brief       Deserializes public parameters from the provided proof data.
         * @param[in]   full_proof_data The byte vector representing the full proof data.
         * @return      A result containing a pair of JSON arrays with the public parameters and empty private parameters
         */
        static outcome::result<std::pair<boost::json::array, boost::json::array>> DeSerializePublicParams(
            const std::vector<uint8_t> &full_proof_data );

        /** 
         * @brief       Constant representing the processing proof type name.
         */
        static constexpr std::string_view PROCESSING_TYPE_NAME = "Processing";

        /**
         * @brief       Registers the deserializer and bytecode for the processing proof type.
         * @return      A boolean indicating successful registration.
         */
        static inline bool Register()
        {
            RegisterDeserializer( std::string( PROCESSING_TYPE_NAME ), &ProcessingProof::DeSerializePublicParams );
            RegisterBytecode( std::string( PROCESSING_TYPE_NAME ), "Bypass" );
            return true;
        }

        /** 
         * @brief       Static variable to ensure registration happens on inclusion of header file.
         */
        static inline bool registered = Register();
    };
}

#endif
