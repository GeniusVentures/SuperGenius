/**
 * @file       EscrowTransaction.hpp
 * @brief      Transaction type used to lock UTXO funds into an escrow address.
 * @date       2024-04-24
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _ESCROW_TRANSACTION_HPP_
#define _ESCROW_TRANSACTION_HPP_

#include <string>
#include "account/GeniusTransaction.hpp"
#include "UTXOStructs.hpp"

namespace sgns
{
    /**
     * @brief Transaction that reserves funds for a job escrow while tracking peer payout metadata.
     */
    class EscrowTransaction : public GeniusTransaction
    {
    public:
        /**
         * @brief Creates a new escrow-hold transaction from signed UTXO parameters.
         * @param[in] params Signed UTXO inputs and escrow/change outputs for the hold.
         * @param[in] amount Total amount locked in escrow.
         * @param[in] dev_addr Developer payout address that receives the post-peer remainder.
         * @param[in] peers_cut Per-peer payout multiplier used when releasing escrow.
         * @param[in] dag DAG metadata shared by all transaction types.
         * @return Escrow transaction with transaction type set and hash populated.
         */
        static EscrowTransaction New( UTXOTxParameters         params,
                                      uint64_t                 amount,
                                      std::string              dev_addr,
                                      uint64_t                 peers_cut,
                                      SGTransaction::DAGStruct dag );

        /**
         * @brief Deserializes a serialized escrow transaction.
         * @param[in] data Serialized @c SGTransaction::EscrowTx bytes.
         * @return Shared pointer to the parsed escrow transaction, or nullptr if parsing fails.
         */
        static std::shared_ptr<EscrowTransaction> DeSerializeByteVector( const std::vector<uint8_t> &data );

        /**
         * @brief Destroys the escrow transaction.
         */
        ~EscrowTransaction() override = default;

        using GeniusTransaction::SerializeByteVector;

        std::vector<uint8_t> SerializeByteVector( const SGTransaction::DAGStruct &dag ) const override;

        using GeniusTransaction::SerializeToEmbeddedTransaction;
        EmbeddedTransaction SerializeToEmbeddedTransaction( const SGTransaction::DAGStruct &dag ) const override;

        /**
         * @brief Returns the number of chunks reserved for peer payouts.
         * @return Number of chunks associated with this escrow transaction.
         */
        uint64_t GetNumChunks() const;

        std::string GetTransactionSpecificPath() const override
        {
            return GetType();
        }

        /**
         * @brief Returns the escrow transaction UTXO inputs and outputs.
         * @return UTXO parameters carried by this escrow hold.
         */
        UTXOTxParameters GetUTXOParameters() const
        {
            return utxo_params_;
        }

        bool HasUTXOParameters() const override
        {
            return true;
        }

        std::optional<UTXOTxParameters> GetUTXOParametersOpt() const override
        {
            return utxo_params_;
        }

        /**
         * @brief Returns the developer payout address.
         * @return Address that receives escrow remainder after peer payouts.
         */
        std::string GetDevAddress() const
        {
            return dev_addr_;
        }

        /**
         * @brief Returns the total amount locked in escrow.
         * @return Total amount locked by this escrow hold.
         */
        uint64_t GetAmount() const
        {
            return amount_;
        }

        /**
         * @brief Returns the configured peer-share multiplier.
         * @return Peer payout multiplier applied during escrow release.
         */
        uint64_t GetPeersCut() const
        {
            return peers_cut_;
        }

    private:
        /**
         * @brief Constructs an escrow-hold transaction from its payload and DAG metadata.
         * @param[in] params Signed UTXO inputs and escrow/change outputs for the hold.
         * @param[in] amount Total amount locked in escrow.
         * @param[in] dev_addr Developer payout address that receives the post-peer remainder.
         * @param[in] peers_cut Per-peer payout multiplier used when releasing escrow.
         * @param[in] dag DAG metadata shared by all transaction types.
         */
        EscrowTransaction( UTXOTxParameters         params,
                           uint64_t                 amount,
                           std::string              dev_addr,
                           uint64_t                 peers_cut,
                           SGTransaction::DAGStruct dag );

        UTXOTxParameters utxo_params_; ///< Signed inputs and outputs for the escrow hold.
        uint64_t         amount_;      ///< Total amount locked in escrow.
        std::string      dev_addr_;    ///< Developer payout address for escrow remainder.
        uint64_t         peers_cut_;   ///< Peer payout multiplier used during escrow release.

        /**
         * @brief Registers the deserializer for the escrow-hold transaction type.
         * @return True when registration completes.
         */
        static bool Register()
        {
            RegisterDeserializer( "escrow-hold", &EscrowTransaction::DeSerializeByteVector );
            return true;
        }

        /**
         * @brief Forces static initialization of the escrow-hold transaction deserializer.
         */
        static inline bool registered = Register();
    };
}

#endif
