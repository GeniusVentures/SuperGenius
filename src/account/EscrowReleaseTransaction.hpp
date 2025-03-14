#ifndef _ESCROW_RELEASE_TRANSACTION_HPP_
#define _ESCROW_RELEASE_TRANSACTION_HPP_

#include "account/IGeniusTransactions.hpp"
#include "account/UTXOTxParameters.hpp"

namespace sgns
{
    class EscrowReleaseTransaction : public IGeniusTransactions
    {
    public:
        // Factory method now accepts an extra parameter: escrow_source.
        static EscrowReleaseTransaction New( UTXOTxParameters         params,
                                             uint64_t                 release_amount,
                                             std::string              release_address,
                                             std::string              escrow_source, // new parameter
                                             SGTransaction::DAGStruct dag );

        // (Optional) Method for deserialization
        static std::shared_ptr<EscrowReleaseTransaction> DeSerializeByteVector( const std::vector<uint8_t> &data );

        ~EscrowReleaseTransaction() override = default;

        // Serializes the transaction to a byte vector
        std::vector<uint8_t> SerializeByteVector() override;

        // Example getters
        UTXOTxParameters GetUTXOParameters() const
        {
            return utxo_params_;
        }

        uint64_t GetReleaseAmount() const
        {
            return release_amount_;
        }

        std::string GetReleaseAddress() const
        {
            return release_address_;
        }

        // New: getter for escrow source.
        std::string GetEscrowSource() const
        {
            return escrow_source_;
        }

        // "Name" of this transaction, used in the path.
        std::string GetTransactionSpecificPath() override
        {
            return GetType();
        }

    private:
        // Private constructor now accepts escrow_source.
        EscrowReleaseTransaction( UTXOTxParameters         params,
                                  uint64_t                 release_amount,
                                  std::string              release_address,
                                  std::string              escrow_source, // new parameter
                                  SGTransaction::DAGStruct dag );

        // Fields
        UTXOTxParameters utxo_params_;
        uint64_t         release_amount_;
        std::string      release_address_;
        std::string      escrow_source_; // new field

        // Registration for deserialization.
        static bool Register()
        {
            RegisterDeserializer( "escrowrelease", &EscrowReleaseTransaction::DeSerializeByteVector );
            return true;
        }

        static inline bool registered_ = Register();
    };
}

#endif // _ESCROW_RELEASE_TRANSACTION_HPP_
