/**
 * @file EscrowReleaseTransaction.hpp
 * @brief Declaration of the EscrowReleaseTransaction class.
 */

#ifndef _ESCROW_RELEASE_TRANSACTION_HPP_
#define _ESCROW_RELEASE_TRANSACTION_HPP_

#include "account/IGeniusTransactions.hpp"
#include "account/UTXOTxParameters.hpp"

namespace sgns
{
    /**
      * @brief Represents a transaction used to release funds from an escrow.
      *
      * This transaction holds the UTXO parameters, the amount to be released, the release address,
      * the escrow source, and the original escrow hash.
      */
    class EscrowReleaseTransaction : public IGeniusTransactions
    {
    public:
        /**
          * @brief Creates a new EscrowReleaseTransaction.
          *
          * @param params UTXO transaction parameters.
          * @param release_amount Amount to be released.
          * @param release_address Address where funds will be sent.
          * @param escrow_source Source of the escrow.
          * @param original_escrow_hash Original hash of the escrow transaction.
          * @param dag DAG structure containing transaction metadata.
          * @return An instance of EscrowReleaseTransaction.
          */
        static EscrowReleaseTransaction New( UTXOTxParameters         params,
                                             uint64_t                 release_amount,
                                             std::string              release_address,
                                             std::string              escrow_source,
                                             std::string              original_escrow_hash,
                                             SGTransaction::DAGStruct dag );

        /**
          * @brief Deserializes a byte vector into an EscrowReleaseTransaction.
          *
          * @param data Serialized transaction data.
          * @return A shared pointer to an EscrowReleaseTransaction instance.
          */
        static std::shared_ptr<EscrowReleaseTransaction> DeSerializeByteVector( const std::vector<uint8_t> &data );

        /**
          * @brief Destructor.
          */
        ~EscrowReleaseTransaction() override = default;

        /**
          * @brief Serializes the transaction to a byte vector.
          *
          * @return A vector of bytes representing the serialized transaction.
          */
        std::vector<uint8_t> SerializeByteVector() override;

        /**
          * @brief Gets the UTXO parameters.
          *
          * @return The UTXO parameters of the transaction.
          */
        UTXOTxParameters GetUTXOParameters() const;

        /**
          * @brief Gets the release amount.
          *
          * @return The amount to be released.
          */
        uint64_t GetReleaseAmount() const;

        /**
          * @brief Gets the release address.
          *
          * @return The address where funds will be released.
          */
        std::string GetReleaseAddress() const;

        /**
          * @brief Gets the escrow source.
          *
          * @return The source address of the escrow.
          */
        std::string GetEscrowSource() const;

        /**
          * @brief Gets the original escrow hash.
          *
          * @return The original hash of the escrow transaction.
          */
        std::string GetOriginalEscrowHash() const;

        /**
          * @brief Gets the transaction-specific path.
          *
          * @return A string representing the transaction path.
          */
        std::string GetTransactionSpecificPath() override;

    private:
        /**
          * @brief Private constructor.
          *
          * @param params UTXO transaction parameters.
          * @param release_amount Amount to be released.
          * @param release_address Address where funds will be sent.
          * @param original_escrow_hash Original hash of the escrow transaction.
          * @param escrow_source Source address of the escrow.
          * @param dag DAG structure containing transaction metadata.
          */
        EscrowReleaseTransaction( UTXOTxParameters         params,
                                  uint64_t                 release_amount,
                                  std::string              release_address,
                                  std::string              escrow_source,
                                  std::string              original_escrow_hash,
                                  SGTransaction::DAGStruct dag );

        UTXOTxParameters utxo_params_;          ///< UTXO parameters for the transaction.
        uint64_t         release_amount_;       ///< Amount to be released.
        std::string      release_address_;      ///< Address to which funds will be released.
        std::string      escrow_source_;        ///< Source address of the escrow.
        std::string      original_escrow_hash_; ///< Original hash of the escrow transaction.

        /**
          * @brief Registers the deserializer for the escrow release transaction.
          *
          * @return True if registration is successful.
          */
        static bool Register()
        {
            RegisterDeserializer( "escrowrelease", &EscrowReleaseTransaction::DeSerializeByteVector );
            return true;
        }

        static inline bool registered_ = Register(); ///< Ensures deserializer registration.
    };
}

#endif // _ESCROW_RELEASE_TRANSACTION_HPP_
