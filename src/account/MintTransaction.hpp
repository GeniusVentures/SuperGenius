/**
 * @file       MintTransaction.hpp
 * @brief      Transaction type used to mint tokens from an external chain reference.
 * @date       2024-03-15
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _MINT_TRANSACTION_HPP_
#define _MINT_TRANSACTION_HPP_

#include <vector>
#include <cstdint>

#include "account/GeniusTransaction.hpp"
#include "account/TokenID.hpp"

namespace sgns
{
    /**
     * @brief Transaction that mints tokens after proving a corresponding source-chain event.
     */
    class MintTransaction final : public GeniusTransaction
    {
    public:
        /**
         * @brief Destroys the mint transaction.
         */
        ~MintTransaction() override = default;

        /**
         * @brief Deserializes a serialized mint transaction.
         * @param[in] data Serialized @c SGTransaction::MintTx bytes.
         * @return Shared pointer to the parsed mint transaction, or nullptr if parsing fails.
         */
        static std::shared_ptr<MintTransaction> DeSerializeByteVector( const std::vector<uint8_t> &data );

        /**
         * @brief Creates a new mint transaction instance.
         * @param[in] new_amount Amount of token units to mint.
         * @param[in] chain_id Source chain identifier associated with the mint event.
         * @param[in] token_id Token identifier for the asset being minted.
         * @param[in] dag DAG metadata shared by all transaction types.
         * @return Mint transaction with the transaction type set and hash populated.
         */
        static MintTransaction New( uint64_t                                        new_amount,
                                    std::string                                     chain_id,
                                    TokenID                                         token_id,
                                    SGTransaction::DAGStruct                        dag );

        using GeniusTransaction::SerializeByteVector;

        using GeniusTransaction::SerializeToEmbeddedTransaction;
        EmbeddedTransaction SerializeToEmbeddedTransaction( const SGTransaction::DAGStruct &dag ) const override;
        /**
         * @brief Serializes the mint transaction payload and DAG metadata.
         * @param[in] dag DAG metadata to serialize into the transaction payload.
         * @return Serialized @c SGTransaction::MintTx bytes.
         */
        std::vector<uint8_t> SerializeByteVector( const SGTransaction::DAGStruct &dag ) const override;

        /**
         * @brief Returns the minted amount.
         * @return Amount of token units minted by this transaction.
         */
        uint64_t GetAmount() const;

        /**
         * @brief Returns the minted token identifier.
         * @return Token identifier for the asset minted by this transaction.
         */
        TokenID GetTokenID() const;

        /**
         * @brief Returns the source chain identifier associated with the mint.
         * @return Source chain identifier used for input validation routing.
         */
        std::string GetChainId() const override;

        /**
         * @brief Returns the transaction-specific storage path component.
         * @return Transaction type string used as the path component.
         */
        std::string GetTransactionSpecificPath() const override
        {
            return GetType();
        }

    private:
        /**
         * @brief Constructs a mint transaction from its payload and DAG metadata.
         * @param[in] new_amount Amount of token units to mint.
         * @param[in] chain_id Source chain identifier associated with the mint event.
         * @param[in] token_id Token identifier for the asset being minted.
         * @param[in] dag DAG metadata shared by all transaction types.
         */
        MintTransaction( uint64_t new_amount, std::string chain_id, TokenID token_id, SGTransaction::DAGStruct dag );

        uint64_t    amount;   ///< Amount of token units minted by this transaction.
        std::string chain_id; ///< Source chain identifier associated with the mint event.
        TokenID     token_id; ///< Token identifier for the asset being minted.

        /**
         * @brief Registers the deserializer for the mint transaction type.
         * @return True when registration completes.
         */
        static bool Register()
        {
            RegisterDeserializer( "mint", &MintTransaction::DeSerializeByteVector );
            return true;
        }

        /**
         * @brief Forces static initialization of the mint transaction deserializer.
         */
        static inline bool registered = Register();
    };
}

#endif
