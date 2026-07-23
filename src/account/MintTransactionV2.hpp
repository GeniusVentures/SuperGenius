/**
 * @file       MintTransactionV2.hpp
 * @brief      Header file of the Version 2 of the Mint transaction class
 * @date       2026-03-19
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef SGNS_MINT_TRANSACTION_V2_HPP
#define SGNS_MINT_TRANSACTION_V2_HPP

#include <vector>
#include <cstdint>

#include "account/GeniusTransaction.hpp"
#include "account/TokenID.hpp"
#include "account/UTXOStructs.hpp"

namespace sgns
{
    /**
     * @brief      Implements a Mint Version 2 transaction
     */
    class MintTransactionV2 final : public GeniusTransaction
    {
    public:
        using GeniusTransaction::SerializeByteVector;

        using GeniusTransaction::SerializeToEmbeddedTransaction;
        EmbeddedTransaction SerializeToEmbeddedTransaction( const SGTransaction::DAGStruct &dag ) const override;
        /**
         * @brief      Destroy the Mint Transaction V 2 object
         */
        ~MintTransactionV2() override = default;

        /**
         * @brief       Deserializes a MintV2 serialized byte vector into an object
         * @param[in]   data The serialized MintV2 data
         * @return      A shared pointer to a MintV2 object
         */
        static std::shared_ptr<MintTransactionV2> DeSerializeByteVector( const std::vector<uint8_t> &data );

        /**
         * @brief       Creates a new MintV2 transaction
         * @param[in]   new_amount The amount to be minted
         * @param[in]   chain_id The chain ID from where the mint came from
         * @param[in]   token_id The token ID
         * @param[in]   dag The DAG structure with the common transaction data
         * @param[in]   mint_inputs Explicit input references for the source-chain burn(s)
         * @param[in]   mint_destination The destination of the Mint
         * @return      A @ref MintTransactionV2
         */
        static MintTransactionV2 New( uint64_t                   new_amount,
                                      std::string                chain_id,
                                      TokenID                    token_id,
                                      SGTransaction::DAGStruct   dag,
                                      std::vector<InputUTXOInfo> mint_inputs,
                                      std::string                mint_destination );

        /**
         * @brief       Serializes the transaction
         * @return      The serialized byte vector
         */
        std::vector<uint8_t> SerializeByteVector( const SGTransaction::DAGStruct &dag ) const override;

        /**
         * @brief       Get the amount of the mint
         * @return      The amount of tokens minted
         */
        uint64_t GetAmount() const;

        /**
         * @brief       Get the Token ID
         * @return      The ID which identifies what token was minted
         */
        TokenID GetTokenID() const;

        /**
         * @brief       Get source chain identifier for bridge mint validation routing
         * @return      Source chain id
         */
        std::string GetChainId() const override;

        /**
         * @brief       Returns the UTXOs
         * @return      The UTXOs of the MintV2 transaction
         */
        UTXOTxParameters GetUTXOParameters() const;

        /**
         * @brief       Returns if transaction supports UTXOs
         * @return      True if supported, false otherwise
         */
        bool HasUTXOParameters() const override;

        /**
         * @brief       Returns the UTXOs
         * @return      If exists, returns the UTXOs of the transaction
         */
        std::optional<UTXOTxParameters> GetUTXOParametersOpt() const override;

        /**
         * @brief       Gets the transaction specific path
         * @return      Returns the transaction specific path
         */
        std::string GetTransactionSpecificPath() const override
        {
            return GetType();
        }

        /**
         * @brief       Returns the topics of interest of this transaction
         * @return      A set of topics
         */
        std::unordered_set<std::string> GetTopics() const override;

        /**
         * @brief       Builds `mint-v2:<chain>:<burn-hash>:<receipt-index>`.
         * @return      Canonical readable preimage or invalid_argument.
         */
        outcome::result<std::string> GetSlotPreimage() const override;

    private:
        /**
         * @brief       Construct a new Mint Transaction V2
         * @param[in]   utxo_params The UTXO set (inputs and outputs)
         * @param[in]   chain_id The chain ID form which the inputs came
         * @param[in]   token_id The Token ID
         * @param[in]   dag The basic DAG structure of every transaction
         */
        MintTransactionV2( UTXOTxParameters         utxo_params,
                           std::string              chain_id,
                           TokenID                  token_id,
                           SGTransaction::DAGStruct dag );

        UTXOTxParameters utxo_params_; ///< The UTXOs (inputs and outputs)
        std::string      chain_id_;    ///< The chain ID from the bridge
        TokenID          token_id_;    ///< The ID of the token minted

        /**
         * @brief       Registers a deserializer for MintV2 transactions
         * @return      Returns true when registered
         */
        static bool Register()
        {
            RegisterDeserializer( "mint-v2", &MintTransactionV2::DeSerializeByteVector );
            return true;
        }

        /**
         * @brief      Forces the static initialization of the Deserializer.
         */
        static inline bool registered = Register();
    };
}

#endif // SGNS_MINT_TRANSACTION_V2_HPP
