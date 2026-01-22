#include "UTXOManager.hpp"

namespace sgns
{
    uint64_t UTXOManager::GetBalance() const
    {
        return GetBalance( address_ );
    }

    uint64_t UTXOManager::GetBalance( const std::string &address ) const
    {
        uint64_t retval = 0;

        // If not a full node and trying to get balance for other addresses, return 0
        if ( !is_full_node_ && address != address_ )
        {
            logger_->error( "Non-full node cannot get balance for other addresses" );
            return 0;
        }

        std::shared_lock lock( utxos_mutex_ );
        if ( auto it = utxos_.find( address ); it != utxos_.end() )
        {
            for ( const auto &curr : it->second )
            {
                if ( !curr.GetLock() )
                {
                    retval += curr.GetAmount();
                }
            }
        }

        return retval;
    }

    uint64_t UTXOManager::GetBalance( const TokenID &token_id ) const
    {
        return GetBalance( token_id, address_ );
    }

    uint64_t UTXOManager::GetBalance( const TokenID &token_id, const std::string &address ) const
    {
        uint64_t balance = 0;

        // If not a full node and trying to get balance for other addresses, return 0
        if ( !is_full_node_ && address != address_ )
        {
            logger_->warn( "Non-full node cannot get balance for other addresses" );
            return 0;
        }

        std::shared_lock lock( utxos_mutex_ );
        if ( auto it = utxos_.find( address ); it != utxos_.end() )
        {
            for ( const auto &utxo : it->second )
            {
                if ( !utxo.GetLock() && token_id.Equals( utxo.GetTokenID() ) )
                {
                    balance += utxo.GetAmount();
                }
            }
        }
        return balance;
    }

    bool UTXOManager::PutUTXO( const GeniusUTXO &new_utxo, const std::string &address )
    {
        // If not a full node and trying to store UTXOs for other addresses, reject
        if ( !is_full_node_ && address != address_ )
        {
            logger_->debug( "Non-full node cannot store UTXOs for other addresses" );
            return false;
        }

        std::unique_lock lock( utxos_mutex_ );
        auto            &utxo_list = utxos_[address];

        bool is_new = true;
        for ( auto &curr : utxo_list )
        {
            if ( new_utxo.GetTxID() != curr.GetTxID() )
            {
                continue;
            }
            if ( new_utxo.GetOutputIdx() != curr.GetOutputIdx() )
            {
                continue;
            }
            //TODO - If it's the same, might be locked, then unlock
            is_new = false;
            break;
        }
        if ( is_new )
        {
            utxo_list.push_back( new_utxo );
        }
        return is_new;
    }

    void UTXOManager::DeleteUTXO( const base::Hash256 &utxo_id, const std::string &address )
    {
        // If not a full node and trying to delete UTXOs for other addresses, reject
        if ( !is_full_node_ && address != address_ )
        {
            logger_->warn( "Non-full node cannot delete UTXOs for other addresses" );
            return;
        }

        std::unique_lock lock( utxos_mutex_ );
        if ( auto it = utxos_.find( address ); it != utxos_.end() )
        {
            auto &utxo_list = it->second;
            for ( auto utxo_it = utxo_list.begin(); utxo_it != utxo_list.end(); )
            {
                if ( utxo_it->GetTxID() == utxo_id )
                {
                    utxo_it = utxo_list.erase( utxo_it );
                    continue;
                }
                ++utxo_it;
            }
        }
    }

    bool UTXOManager::ConsumeUTXOs( const std::vector<InputUTXOInfo> &infos )
    {
        bool             consumed = false;
        std::unique_lock lock( utxos_mutex_ );
        for ( auto &[_, utxo_list] : utxos_ )
        {
            auto old_size = utxo_list.size();
            utxo_list.erase(
                std::remove_if( utxo_list.begin(),
                                utxo_list.end(),
                                [&infos]( const GeniusUTXO &x )
                                {
                                    return std::any_of(
                                        infos.begin(),
                                        infos.end(),
                                        [&x]( const InputUTXOInfo &a )
                                        { return a.txid_hash_ == x.GetTxID() && a.output_idx_ == x.GetOutputIdx(); } );
                                } ),
                utxo_list.end() );
            if ( utxo_list.size() != old_size )
            {
                consumed = true;
            }
        }
        return consumed;
    }

    std::vector<GeniusUTXO> UTXOManager::GetUTXOs( const std::string &address ) const
    {
        std::shared_lock lock( utxos_mutex_ );
        if ( auto it = utxos_.find( address ); it != utxos_.end() )
        {
            return it->second;
        }
        return {};
    }

    void UTXOManager::SetUTXOs( const std::vector<GeniusUTXO> &utxos, const std::string &address )
    {
        // If not a full node and trying to set UTXOs for other addresses, reject
        if ( !is_full_node_ && address != address_ )
        {
            logger_->warn( "Non-full node cannot set UTXOs for other addresses" );
            return;
        }

        std::unique_lock lock( utxos_mutex_ );
        utxos_[address] = utxos;

        logger_->debug( "Set {} UTXOs for address {}", utxos.size(), address.substr( 0, 8 ) );
    }

    outcome::result<UTXOTxParameters> UTXOManager::CreateTxParameter( uint64_t           amount,
                                                                      const std::string &dest_address,
                                                                      const TokenID     &token_id )
    {
        std::vector<InputUTXOInfo>  inputs;
        std::vector<OutputDestInfo> outputs;

        // Select UTXOs until we cover the requested amount
        std::vector<GeniusUTXO> selected_utxos;
        uint64_t                selected_amount = 0;

        std::unique_lock lock( utxos_mutex_ );
        for ( const auto &utxo : utxos_[address_] )
        {
            if ( selected_amount >= amount )
            {
                break;
            }
            if ( utxo.GetLock() )
            {
                continue;
            }
            if ( !token_id.Equals( utxo.GetTokenID() ) )
            {
                continue;
            }
            inputs.push_back( { utxo.GetTxID(), utxo.GetOutputIdx() } );
            selected_utxos.push_back( utxo );
            selected_amount += utxo.GetAmount();
        }
        lock.unlock();

        // Abort if insufficient funds
        if ( selected_amount < amount || selected_utxos.empty() )
        {
            inputs.clear();
            outputs.clear();
            return outcome::failure( std::errc::invalid_argument );
        }

        // Aggregate destination amounts by TokenID
        uint64_t send_totals = 0;
        uint64_t used_amount = 0;
        for ( const auto &utxo : selected_utxos )
        {
            uint64_t utxo_amt = utxo.GetAmount();

            if ( used_amount + utxo_amt <= amount )
            {
                send_totals += utxo_amt;
                used_amount += utxo_amt;
            }
            else
            {
                uint64_t needed  = amount - used_amount;
                send_totals     += needed;
                used_amount     += needed;
                break;
            }
        }

        // Reserve space: one output per token plus possible change
        outputs.reserve( 2 );

        // Create one output per TokenID for the destination

        outputs.push_back( { send_totals, dest_address, token_id } );

        // Single change output (always from the last UTXO's token)
        uint64_t change = selected_amount - amount;
        if ( change > 0 )
        {
            outputs.push_back( { change, address_, token_id } );
        }

        SignInputs( inputs );

        return std::make_pair( inputs, outputs );
    }

    outcome::result<UTXOTxParameters> UTXOManager::CreateTxParameter( const std::vector<OutputDestInfo> &destinations,
                                                                      const TokenID                     &token_id )
    {
        std::vector<InputUTXOInfo>  inputs;
        std::vector<OutputDestInfo> outputs;

        uint64_t total_amount = 0;
        TokenID  change_token;

        for ( const auto &d : destinations )
        {
            total_amount += d.encrypted_amount;
        }

        uint64_t         used_amount = 0;
        std::unique_lock lock( utxos_mutex_ );
        for ( const auto &utxo : utxos_[address_] )
        {
            if ( used_amount >= total_amount )
            {
                break;
            }
            if ( utxo.GetLock() )
            {
                continue;
            }
            if ( !token_id.Equals( utxo.GetTokenID() ) )
            {
                continue;
            }
            InputUTXOInfo curr_input{ utxo.GetTxID(), utxo.GetOutputIdx(), {} };
            used_amount += utxo.GetAmount();
            inputs.push_back( curr_input );
            change_token = token_id;
        }
        lock.unlock();

        if ( used_amount < total_amount )
        {
            inputs.clear();
            outputs.clear();
        }
        else
        {
            outputs = destinations;
        }

        if ( used_amount > total_amount )
        {
            uint64_t change = used_amount - total_amount;
            outputs.push_back( { change, address_, change_token } );
        }

        SignInputs( inputs );

        return std::make_pair( inputs, outputs );
    }

    void UTXOManager::ReserveUTXOs( const std::vector<InputUTXOInfo> &inputs )
    {
        std::unique_lock lock( utxos_mutex_ );

        for ( auto &utxo : utxos_[address_] )
        {
            for ( auto &input_utxo : inputs )
            {
                if ( input_utxo.txid_hash_ == utxo.GetTxID() )
                {
                    utxo.SetLocked( true );
                }
            }
        }
    }

    void UTXOManager::RollbackUTXOs( const std::vector<InputUTXOInfo> &inputs )
    {
        std::unique_lock lock( utxos_mutex_ );

        for ( auto &utxo : utxos_[address_] )
        {
            for ( auto &input_utxo : inputs )
            {
                if ( input_utxo.txid_hash_ == utxo.GetTxID() )
                {
                    utxo.SetLocked( false );
                }
            }
        }
    }

    void UTXOManager::SignInputs( std::vector<InputUTXOInfo> &inputs ) const
    {
        for ( auto &input : inputs )
        {
            auto serialized  = input.SerializeForSigning();
            auto signature   = sign_( serialized );
            input.signature_ = signature;
        }
    }
}
