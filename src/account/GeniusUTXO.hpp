/**
 * @file       GeniusUTXO.hpp
 * @brief      Lightweight value type representing a spendable UTXO entry and its outpoint.
 * @date       2024-04-25
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _GENIUS_UTXO_HPP
#define _GENIUS_UTXO_HPP

#include "base/blob.hpp"
#include "account/TokenID.hpp"

#include <string>
#include <utility>

namespace sgns
{
    /**
     * @brief Unique identifier for a transaction output.
     */
    struct OutPoint
    {
        base::Hash256 txid_hash_;      ///< Hash of the transaction that produced the output.
        uint32_t      output_idx_{ 0 }; ///< Output index within the producing transaction.

        /**
         * @brief Compares two outpoints for exact transaction-and-index equality.
         * @param[in] other Outpoint to compare against.
         * @return True when both the transaction hash and output index match.
         */
        bool operator==( const OutPoint &other ) const
        {
            return txid_hash_ == other.txid_hash_ && output_idx_ == other.output_idx_;
        }
    };

    /**
     * @brief Immutable-style UTXO value object containing ownership, token, amount, and outpoint metadata.
     */
    class GeniusUTXO
    {
    public:
        /**
         * @brief Constructs an empty UTXO placeholder.
         *
         * The placeholder has a default outpoint, zero amount, default token identifier,
         * and no owner address.
         */
        GeniusUTXO() : outpoint_{}, amount_( 0 ), token_id_(), owner_address_()
        {
        }

        /**
         * @brief Constructs a UTXO without an owner address.
         * @param[in] hash Hash of the transaction that produced this output.
         * @param[in] previous_index Output index within the producing transaction.
         * @param[in] amount Amount carried by the output.
         * @param[in] token_id Token identifier carried by the output.
         */
        GeniusUTXO( const base::Hash256 &hash, uint32_t previous_index, uint64_t amount, TokenID token_id ) :
            outpoint_{ hash, previous_index }, //
            amount_( amount ),                 //
            token_id_( token_id )              //
        {
        }

        /**
         * @brief Constructs a fully specified UTXO.
         * @param[in] hash Hash of the transaction that produced this output.
         * @param[in] previous_index Output index within the producing transaction.
         * @param[in] amount Amount carried by the output.
         * @param[in] token_id Token identifier carried by the output.
         * @param[in] owner_address Address that owns or can spend the output.
         */
        GeniusUTXO( const base::Hash256 &hash,
                    uint32_t             previous_index,
                    uint64_t             amount,
                    TokenID              token_id,
                    std::string          owner_address ) :
            outpoint_{ hash, previous_index }, //
            amount_( amount ),                 //
            token_id_( token_id ),             //
            owner_address_( std::move( owner_address ) )
        {
        }

        /**
         * @brief Sets the owner address associated with this UTXO.
         * @param[in] owner_address Address that owns or can spend the output.
         */
        void SetOwnerAddress( std::string owner_address )
        {
            owner_address_ = std::move( owner_address );
        }

        /**
         * @brief Returns the owner address associated with this UTXO.
         * @return Address that owns or can spend the output; empty when not set.
         */
        const std::string &GetOwnerAddress() const
        {
            return owner_address_;
        }

        /**
         * @brief Returns the full outpoint descriptor.
         * @return Outpoint containing the producing transaction hash and output index.
         */
        OutPoint GetOutPoint() const
        {
            return outpoint_;
        }

        /**
         * @brief Returns the originating transaction id.
         * @return Hash of the transaction that produced this output.
         */
        base::Hash256 GetTxID() const
        {
            return outpoint_.txid_hash_;
        }

        /**
         * @brief Returns the output index within the originating transaction.
         * @return Output index within the producing transaction.
         */
        uint32_t GetOutputIdx() const
        {
            return outpoint_.output_idx_;
        }

        /**
         * @brief Returns the unencrypted amount represented by the UTXO.
         * @return Amount carried by the output.
         */
        uint64_t GetAmount() const
        {
            return amount_;
        }

        /**
         * @brief Returns the token identifier associated with the UTXO.
         * @return Token identifier carried by the output.
         */
        TokenID GetTokenID() const
        {
            return token_id_;
        }

    private:
        OutPoint    outpoint_;      ///< Producing transaction hash and output index.
        uint64_t    amount_;        ///< Amount carried by the output.
        TokenID     token_id_;      ///< Token identifier carried by the output.
        std::string owner_address_; ///< Address that owns or can spend the output.
    };
}

#endif
