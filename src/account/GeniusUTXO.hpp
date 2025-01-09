/**
 * @file       GeniusUTXO.hpp
 * @brief      
 * @date       2024-04-25
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _GENIUS_UTXO_HPP
#define _GENIUS_UTXO_HPP

#include "base/blob.hpp"
#include "base/util.hpp"

namespace sgns
{
    class GeniusUTXO
    {
    public:
        GeniusUTXO( const base::Hash256 &hash, uint32_t previous_index, double amount ) :
            txid_hash_( hash ),            //
            output_idx_( previous_index ), //
            amount_( RoundTo5Digits(amount) ),             //
            locked_( false )               //
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

        double GetAmount() const
        {
            return amount_;
        }

        bool GetLock() const
        {
            return locked_;
        }

    private:
        base::Hash256 txid_hash_;
        uint32_t      output_idx_;
        double        amount_;
        bool          locked_;
    };
}

#endif
