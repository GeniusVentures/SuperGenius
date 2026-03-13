/**
 * @file       GeniusUTXO.hpp
 * @brief      
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
    struct OutPoint
    {
        base::Hash256 txid_hash_;
        uint32_t      output_idx_{ 0 };

        bool operator==( const OutPoint &other ) const
        {
            return txid_hash_ == other.txid_hash_ && output_idx_ == other.output_idx_;
        }
    };

    class GeniusUTXO
    {
    public:
        GeniusUTXO() : outpoint_{}, amount_( 0 ), token_id_(), owner_address_()
        {
        }

        GeniusUTXO( const base::Hash256 &hash, uint32_t previous_index, uint64_t amount, TokenID token_id ) :
            outpoint_{ hash, previous_index }, //
            amount_( amount ),                 //
            token_id_( token_id )              //
        {
        }

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

        void SetOwnerAddress( std::string owner_address )
        {
            owner_address_ = std::move( owner_address );
        }

        const std::string &GetOwnerAddress() const
        {
            return owner_address_;
        }

        OutPoint GetOutPoint() const
        {
            return outpoint_;
        }

        base::Hash256 GetTxID() const
        {
            return outpoint_.txid_hash_;
        }

        uint32_t GetOutputIdx() const
        {
            return outpoint_.output_idx_;
        }

        uint64_t GetAmount() const
        {
            return amount_;
        }

        TokenID GetTokenID() const
        {
            return token_id_;
        }

    private:
        OutPoint      outpoint_;
        uint64_t      amount_;
        TokenID       token_id_;
        std::string   owner_address_;
    };
}

#endif
