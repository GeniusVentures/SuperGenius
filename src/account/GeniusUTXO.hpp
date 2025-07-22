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

namespace sgns
{
    class GeniusUTXO
    {
    public:
        GeniusUTXO( const base::Hash256 &hash, uint32_t previous_index, uint64_t amount, TokenID token_id ) :
            txid_hash_( hash ),            //
            output_idx_( previous_index ), //
            amount_( amount ),             //
            locked_( false ),              //
            tokein_id_( token_id )         //
        {
        }

        void ToggleLock( bool lock )
        {
            locked_ = lock;
        }

        base::Hash256 GetTxID() const
        {
            return txid_hash_;
        }

        uint32_t GetOutputIdx() const
        {
            return output_idx_;
        }

        uint64_t GetAmount() const
        {
            return amount_;
        }

        bool GetLock() const
        {
            return locked_;
        }

        TokenID GetTokenID() const
        {
            return tokein_id_;
        }

    private:
        base::Hash256 txid_hash_;
        uint32_t      output_idx_;
        uint64_t      amount_;
        bool          locked_;
        TokenID       tokein_id_;
    };
}

#endif
