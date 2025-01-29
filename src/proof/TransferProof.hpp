/**
 * @file       TransferProof.hpp
 * @brief      Derived class for generating and verifying transfer proofs
 * @date       2024-09-29
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _TRANSFER_PROOF_HPP_
#define _TRANSFER_PROOF_HPP_

#include <string>
#include <cstdint>
#include <array>
#include <utility>
#include <boost/json.hpp>
#include <string_view>
#include "IBasicProof.hpp"
#include "circuits/TransactionVerifierCircuit.hpp"

namespace sgns
{
    /**
     * @class       TransferProof
     * @brief       A class for generating and verifying transfer proofs.
     *
     *              TransferProof is a derived class from IBasicProof, providing the specific
     *              implementation for handling transfer of funds proof generation and verification.
     */
    class TransferProof : public IBasicProof
    {
    public:
        /**
         * @brief       Constructor for the TransferProof class.
         * @param[in]   balance The balance of the account for the transfer.
         * @param[in]   amount The amount to be transferred.
         */
        explicit TransferProof( uint64_t balance, uint64_t amount );

        /**
         * @brief       Destructor for the TransferProof class.
         */
        ~TransferProof() = default;

        /**
         * @brief       Retrieves the type of proof for transfers.
         * @return      A string representing the type of the proof.
         */
        std::string GetProofType() const override
        {
            return std::string( TRANSFER_TYPE_NAME );
        }

        /**
         * @brief       Generates a JSON object for a curve parameter.
         * @tparam      T The type of the curve parameter.
         * @param       value The curve value to be converted to a JSON object.
         * @return      A JSON object containing the curve parameter.
         */
        template <typename T>
        static boost::json::object GenerateCurveParameter( T value );

    protected:
        static constexpr uint64_t generator_X_point = 1;     ///< X coordinate of the generator point
        static constexpr uint64_t generator_Y_point = 2;     ///< Y coordinate of the generator point
        static constexpr uint64_t base_seed         = 12345; ///< Base seed
        static constexpr uint64_t provided_totp     = 67890; ///< Provided TOTP
        
        /// Array of range values used in transfer proofs.
        static constexpr std::array<uint64_t, 4> ranges = { 1000, 2000, 3000, 4000 };
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

    private:
        uint64_t balance_; ///< The balance associated with the transfer.
        uint64_t amount_;  ///< The amount to be transferred.

        /**
         * @brief       Deserializes public parameters from the provided proof data.
         * @param[in]   full_proof_data The byte vector representing the full proof data.
         * @return      A result containing a pair of JSON arrays with the public parameters and empty private parameters
         */
        static outcome::result<std::pair<boost::json::array, boost::json::array>> DeSerializePublicParams(
            const std::vector<uint8_t> &full_proof_data );

        /** 
         * @brief       Constant representing the transfer proof type name.
         */
        static constexpr std::string_view TRANSFER_TYPE_NAME = "Transfer";

        /**
         * @brief       Registers the deserializer and bytecode for the transfer proof type.
         * @return      A boolean indicating successful registration.
         */
        static inline bool Register()
        {
            RegisterDeserializer( std::string( TRANSFER_TYPE_NAME ), &TransferProof::DeSerializePublicParams );
#ifdef RELEASE_BYTECODE_CIRCUITS
            RegisterBytecode( std::string( TRANSFER_TYPE_NAME ), std::string( TransactionCircuit ) );
#else
            RegisterBytecode( std::string( TRANSFER_TYPE_NAME ), std::string( TransactionCircuitDebug ) );
#endif

            return true;
        }

        /** 
         * @brief       Static variable to ensure registration happens on inclusion of header file.
         */
        static inline bool registered = Register();
    };
}

#endif
