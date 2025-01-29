/**
 * @file       RecursiveTransferProof.hpp
 * @brief      Header file of the RecursiveTransferProof
 * @date       2025-01-29
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _RECURSIVE_TRANSFER_PROOF_HPP_
#define _RECURSIVE_TRANSFER_PROOF_HPP_

#include <string>
#include <cstdint>
#include <array>
#include <utility>
#include <boost/json.hpp>
#include <string_view>
#include "GeniusProver.hpp"
#include "IBasicProof.hpp"
#include "TransferProof.hpp"

#include "circuits/RecursiveTransactionCircuit.hpp"

namespace sgns
{
    /**
     * @class       RecursiveTransferProof
     * @brief       A class for generating a recursive Transfer Proof
     *
     *              RecursiveTransferProof is a derived class from IBasicProof, providing the specific
     *              implementation for generating the parameters to create a recursive snark
     */
    class RecursiveTransferProof : public TransferProof
    {
    public:
        /**
         * @brief       Constructor for the RecursiveTransferProof class.
         * @param[in]   balance The balance of the account for the transfer.
         * @param[in]   amount The amount to be transferred.
         */
        explicit RecursiveTransferProof( uint64_t balance, uint64_t amount, GeniusProver::ProofSnarkType snark );

        /**
         * @brief       Destructor for the RecursiveTransferProof class.
         */
        ~RecursiveTransferProof() = default;

        /**
         * @brief       Retrieves the type of proof for recursive transfers.
         * @return      A string representing the type of the proof.
         */
        std::string GetProofType() const override
        {
            return std::string( RECURSIVE_TRANSFER_TYPE_NAME );
        }

    protected:
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
        static constexpr std::string_view RECURSIVE_TRANSFER_TYPE_NAME = "RecursiveTransfer";

        /**
         * @brief       Registers the deserializer and bytecode for the recursive transfer proof type.
         * @return      A boolean indicating successful registration.
         */
        static inline bool Register()
        {
            RegisterDeserializer( std::string( RECURSIVE_TRANSFER_TYPE_NAME ), &RecursiveTransferProof::DeSerializePublicParams );
#ifdef RELEASE_BYTECODE_CIRCUITS
            RegisterBytecode( std::string( RECURSIVE_TRANSFER_TYPE_NAME ), std::string( RecursiveTransactionCircuit ) );
#else
            RegisterBytecode( std::string( RECURSIVE_TRANSFER_TYPE_NAME ), std::string( RecursiveTransactionCircuit ) );
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
