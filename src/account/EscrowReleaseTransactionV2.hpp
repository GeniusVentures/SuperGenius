/**
 * @file EscrowReleaseTransactionV2.hpp
 * @brief Signed escrow hold payment and canonical outpoint identity.
 */
#ifndef SGNS_ESCROW_RELEASE_TRANSACTION_V2_HPP
#define SGNS_ESCROW_RELEASE_TRANSACTION_V2_HPP

#include "account/GeniusTransaction.hpp"
#include "account/UTXOStructs.hpp"

namespace sgns
{
    /** @brief Dedicated funds-moving escrow release protocol. */
    class EscrowReleaseTransactionV2 final : public GeniusTransaction
    {
    public:
        /** @brief Releases payment data through the transaction interface. */
        ~EscrowReleaseTransactionV2() override = default;
        /** @brief Constructs and hashes a payment; certified hold validation belongs to input admission. */
        static EscrowReleaseTransactionV2 New( std::vector<InputUTXOInfo>  inputs,
                                               std::vector<OutputDestInfo> destinations,
                                               SGTransaction::DAGStruct    dag );

        using GeniusTransaction::SerializeByteVector;
        /** @brief Serializes the full payment with the supplied DAG metadata. */
        std::vector<uint8_t> SerializeByteVector( const SGTransaction::DAGStruct &dag ) const override;

        using GeniusTransaction::SerializeToEmbeddedTransaction;
        /** @brief Wraps the same payment message in dedicated oneof tag 10. */
        EmbeddedTransaction SerializeToEmbeddedTransaction( const SGTransaction::DAGStruct &dag ) const override;

        /** @brief Decodes checked V2 payment shapes, returning nullptr for malformed bytes. */
        static std::shared_ptr<EscrowReleaseTransactionV2> DeSerializeByteVector( const std::vector<uint8_t> &data );

        /** @brief Returns the sole hold input and payout destinations. */
        UTXOTxParameters GetUTXOParameters() const;
        /** @brief Returns the hold input descriptors. */
        std::vector<InputUTXOInfo> GetInputInfos() const;
        /** @brief Returns the payout destinations. */
        std::vector<OutputDestInfo> GetDstInfos() const;
        /** @brief Identifies V2 as a funds-moving UTXO transaction. */
        bool HasUTXOParameters() const override;
        /** @brief Exposes payment parameters to the generic UTXO lifecycle. */
        std::optional<UTXOTxParameters> GetUTXOParametersOpt() const override;
        /** @brief Returns the explicit protocol-version path. */
        std::string GetTransactionSpecificPath() const override;
        /** @brief Returns source and payout destination notification topics. */
        std::unordered_set<std::string> GetTopics() const override;
        /** @brief Returns escrow-release-v2:<normalized hold hash>:0, or empty for an invalid input shape. */
        std::string GetSlotID() const override;

    private:
        /** @brief Retains the payment and its signed DAG metadata. */
        EscrowReleaseTransactionV2( UTXOTxParameters params, SGTransaction::DAGStruct dag );
        /** @brief Single message population routine for raw and embedded serialization. */
        SGTransaction::EscrowReleaseTxV2 PopulateMessage( const SGTransaction::DAGStruct &dag ) const;

        UTXOTxParameters  utxo_params_; ///< Actual hold input and payment outputs.
        static const bool registered_;  ///< Registers the codec independently of manager dispatch.
    };
}

#endif // SGNS_ESCROW_RELEASE_TRANSACTION_V2_HPP
