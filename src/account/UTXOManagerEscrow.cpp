/**
 * @file UTXOManagerEscrow.cpp
 * @brief Typed escrow release verification against the local UTXO state.
 */
#include "account/UTXOManager.hpp"

#include <algorithm>

#include "account/EscrowReleaseTransactionV2.hpp"
#include "account/EscrowTransaction.hpp"
#include "account/InputValidators.hpp"
#include "base/blob.hpp"

namespace sgns
{
    bool UTXOManager::VerifyEscrowReleaseParameters( const EscrowReleaseTransactionV2 &release,
                                                     const EscrowTransaction          &hold ) const
    {
        const auto params      = release.GetUTXOParameters();
        const auto hold_params = hold.GetUTXOParameters();
        const auto hash        = base::Hash256::fromHex( hold.GetHash() );
        if ( !hold.CheckHash() || hash.has_error() || params.first.size() != 1 || params.second.empty() ||
             hold_params.second.empty() || params.first.front().txid_hash_ != hash.value() ||
             params.first.front().output_idx_ != input_validator_constants::ESCROW_LOCK_OUTPUT_INDEX )
        {
            return false;
        }
        const auto            &locked = hold_params.second.front();
        const std::shared_lock lock( utxos_mutex_ );
        const auto             tracked = utxo_outpoints_.find(
            OutPoint{ hash.value(), input_validator_constants::ESCROW_LOCK_OUTPUT_INDEX } );
        if ( !utxo_address::IsEscrowLockAddress( locked.dest_address ) || tracked == utxo_outpoints_.end() ||
             tracked->second.utxo.GetAmount() != locked.encrypted_amount ||
             !tracked->second.utxo.GetTokenID().Equals( locked.token_id ) )
        {
            return false;
        }
        if ( std::any_of( params.second.begin(),
                          params.second.end(),
                          []( const OutputDestInfo &output )
                          { return output.dest_address.empty() || output.token_id.size() == 0; } ) )
        {
            return false;
        }
        return VerifyParametersForOwner( params, release.GetSrcAddress(), locked.dest_address );
    }
}
