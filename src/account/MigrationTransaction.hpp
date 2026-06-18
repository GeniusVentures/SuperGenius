/**
 * @file       MigrationTransaction.hpp
 * @brief      Header file for a Migration transaction that mint tokens on the destination chain based on observed legacy balances on the source chain.
 * @date       2026-04-29
 */
#ifndef SGNS_MIGRATION_TRANSACTION_HPP
#define SGNS_MIGRATION_TRANSACTION_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "account/GeniusTransaction.hpp"
#include "account/TokenID.hpp"
#include "account/UTXOStructs.hpp"

namespace sgns
{
    class MigrationTransaction final : public GeniusTransaction
    {
    public:
        using GeniusTransaction::SerializeByteVector;

        using GeniusTransaction::SerializeToEmbeddedTransaction;
        EmbeddedTransaction SerializeToEmbeddedTransaction( const SGTransaction::DAGStruct &dag ) const override;

        ~MigrationTransaction() override = default;

        static std::shared_ptr<MigrationTransaction> DeSerializeByteVector( const std::vector<uint8_t> &data );

        static MigrationTransaction New( uint64_t                 amount,
                                         std::string              from_version,
                                         TokenID                  token_id,
                                         SGTransaction::DAGStruct dag,
                                         std::string              destination = "" );

        std::vector<uint8_t> SerializeByteVector( const SGTransaction::DAGStruct &dag ) const override;

        uint64_t GetAmount() const;
        TokenID  GetTokenID() const;

        std::string GetChainId() const override;

        UTXOTxParameters GetUTXOParameters() const;
        bool             HasUTXOParameters() const override;
        std::optional<UTXOTxParameters> GetUTXOParametersOpt() const override;

        std::string GetFromVersion() const;

        std::string GetTransactionSpecificPath() const override
        {
            return GetType();
        }

        std::unordered_set<std::string> GetTopics() const override;

        static std::string DeriveUniqueSourceKey( std::string_view from_version,
                                                  std::string_view source_address,
                                                  const TokenID   &token_id );

    private:
        MigrationTransaction( UTXOTxParameters         utxo_params,
                              std::string              from_version,
                              TokenID                  token_id,
                              SGTransaction::DAGStruct dag );

        UTXOTxParameters utxo_params_;
        std::string      from_version_;
        TokenID          token_id_;

        static bool Register()
        {
            RegisterDeserializer( "migration", &MigrationTransaction::DeSerializeByteVector );
            return true;
        }

        static inline bool registered = Register();
    };
}

#endif // SGNS_MIGRATION_TRANSACTION_HPP
