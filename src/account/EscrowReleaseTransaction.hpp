/**
 * @file EscrowReleaseTransaction.hpp
 * @brief Faithful historical escrow-release status-marker representation.
 */
#ifndef SGNS_ESCROW_RELEASE_TRANSACTION_HPP
#define SGNS_ESCROW_RELEASE_TRANSACTION_HPP

#include "account/GeniusTransaction.hpp"
#include "account/UTXOStructs.hpp"

namespace sgns
{
    /** @brief Legacy signed marker; its UTXO fields describe history and never expose payment effects. */
    class EscrowReleaseTransaction final : public GeniusTransaction
    {
    public:
        /** @brief Releases marker data through the transaction interface. */
        ~EscrowReleaseTransaction() override = default;
        /** @brief Constructs and hashes the historical six-field marker representation. */
        static EscrowReleaseTransaction New( UTXOTxParameters         params,
                                             uint64_t                 release_amount,
                                             std::string              release_address,
                                             std::string              escrow_source,
                                             std::string              original_escrow_hash,
                                             SGTransaction::DAGStruct dag );

        using GeniusTransaction::SerializeByteVector;
        /** @brief Serializes marker fields with the supplied DAG without recomputing signed metadata. */
        std::vector<uint8_t> SerializeByteVector( const SGTransaction::DAGStruct &dag ) const override;
        using GeniusTransaction::SerializeToEmbeddedTransaction;
        /** @brief Wraps the historical marker in unchanged embedded tag 7. */
        EmbeddedTransaction SerializeToEmbeddedTransaction( const SGTransaction::DAGStruct &dag ) const override;
        /** @brief Decodes a legacy marker without converting it to an escrow hold or payment. */
        static std::shared_ptr<EscrowReleaseTransaction> DeSerializeByteVector( const std::vector<uint8_t> &data );

        /** @brief Returns historical UTXO marker data, outside the generic payment interface. */
        UTXOTxParameters GetUTXOParameters() const;
        /** @brief Returns the historical marker's release amount. */
        uint64_t GetReleaseAmount() const;
        /** @brief Returns the historical marker's release address. */
        std::string GetReleaseAddress() const;
        /** @brief Returns the original escrow's source address. */
        std::string GetEscrowSource() const;
        /** @brief Returns the historical marker's original escrow hash. */
        std::string GetOriginalEscrowHash() const;
        /** @brief Returns the legacy protocol path. */
        std::string GetTransactionSpecificPath() const override;
        /** @brief Returns the DAG and original escrow source notification topics. */
        std::unordered_set<std::string> GetTopics() const override;

    private:
        /** @brief Retains all historical fields without assigning payment semantics. */
        EscrowReleaseTransaction( UTXOTxParameters         params,
                                  uint64_t                 release_amount,
                                  std::string              release_address,
                                  std::string              escrow_source,
                                  std::string              original_escrow_hash,
                                  SGTransaction::DAGStruct dag );
        /** @brief Single legacy population routine for raw and embedded representations. */
        SGTransaction::EscrowReleaseTx PopulateMessage( const SGTransaction::DAGStruct &dag ) const;

        UTXOTxParameters  utxo_params_;          ///< Historical marker data, not generic payment parameters.
        uint64_t          release_amount_;       ///< Historical reported amount.
        std::string       release_address_;      ///< Historical reported recipient.
        std::string       escrow_source_;        ///< Original escrow source.
        std::string       original_escrow_hash_; ///< Original escrow identity.
        static const bool registered_;           ///< Registers the codec independently of manager dispatch.
    };
}

#endif // SGNS_ESCROW_RELEASE_TRANSACTION_HPP
