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
            for ( const auto &[state, curr] : it->second )
            {
                if ( !curr.GetLock() && state == UTXOState::UTXO_READY )
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
            for ( const auto &[state, utxo] : it->second )
            {
                if ( !utxo.GetLock() && token_id.Equals( utxo.GetTokenID() ) && state == UTXOState::UTXO_READY )
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
        for ( auto it = utxo_list.begin(); it != utxo_list.end(); )
        {
            auto &[state, curr] = *it;
            if ( new_utxo.GetTxID() != curr.GetTxID() )
            {
                ++it;
                continue;
            }
            if ( new_utxo.GetOutputIdx() != curr.GetOutputIdx() )
            {
                ++it;
                continue;
            }
            if ( state == UTXOState::UTXO_CONSUMED )
            {
                it     = utxo_list.erase( it );
                is_new = false;
                break;
            }
            //TODO - If it's the same, might be locked, then unlock
            is_new = false;
            break;
        }
        if ( is_new )
        {
            utxo_list.push_back( { UTXOState::UTXO_READY, new_utxo } );
        }
        return is_new;
    }

    void UTXOManager::DeleteUTXO( const base::Hash256 &utxo_id, const std::string &address )
    {
        // If not a full node and trying to delete UTXOs for other addresses, reject
        if ( !is_full_node_ && address != address_ )
        {
            logger_->warn( "Non-full node cannot delete UTXOs for other addresses" );
            //return;
        }

        std::unique_lock lock( utxos_mutex_ );
        if ( auto it = utxos_.find( address ); it != utxos_.end() )
        {
            auto &utxo_list = it->second;
            for ( auto utxo_it = utxo_list.begin(); utxo_it != utxo_list.end(); )
            {
                auto &[state, curr] = *utxo_it;
                if ( curr.GetTxID() == utxo_id )
                {
                    utxo_it = utxo_list.erase( utxo_it );
                    continue;
                }
                ++utxo_it;
            }
        }
    }

    bool UTXOManager::ConsumeUTXOs( const std::vector<InputUTXOInfo> &infos, const std::string &address )
    {
        bool             consumed = true;
        std::unique_lock lock( utxos_mutex_ );
        auto            &utxo_list = utxos_[address];
        for ( auto &input_info : infos )
        {
            bool utxo_found = false;
            auto utxo_it    = utxo_list.end();
            for ( auto it = utxo_list.begin(); it != utxo_list.end(); ++it )
            {
                auto &[state, curr] = *it;
                if ( input_info.txid_hash_ != curr.GetTxID() )
                {
                    continue;
                }
                if ( input_info.output_idx_ != curr.GetOutputIdx() )
                {
                    continue;
                }
                utxo_found = true;
                utxo_it    = it;
                break;
            }
            if ( utxo_found )
            {
                utxo_list.erase( utxo_it );
            }
            else
            {
                GeniusUTXO consumed_utxo( input_info.txid_hash_, input_info.output_idx_, 0, TokenID() );
                utxo_list.emplace_back( UTXOState::UTXO_CONSUMED, consumed_utxo );
            }
            consumed = consumed && utxo_found;
        }

        return consumed;
    }

    std::vector<GeniusUTXO> UTXOManager::GetUTXOs( const std::string &address ) const
    {
        std::shared_lock lock( utxos_mutex_ );
        if ( auto it = utxos_.find( address ); it != utxos_.end() )
        {
            std::vector<GeniusUTXO> result;
            result.reserve( it->second.size() );
            for ( const auto &[state, utxo] : it->second )
            {
                if ( state == UTXOState::UTXO_CONSUMED )
                {
                    continue;
                }
                result.push_back( utxo );
            }
            return result;
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
        auto            &utxo_list = utxos_[address];
        utxo_list.clear();
        utxo_list.reserve( utxos.size() );
        for ( const auto &utxo : utxos )
        {
            utxo_list.emplace_back( UTXOState::UTXO_READY, utxo );
        }

        logger_->debug( "Set {} UTXOs for address {}", utxos.size(), address.substr( 0, 8 ) );
    }

    outcome::result<UTXOTxParameters> UTXOManager::CreateTxParameter( uint64_t           amount,
                                                                      const std::string &dest_address,
                                                                      const TokenID     &token_id )
    {
        std::vector<InputUTXOInfo>  inputs_;
        std::vector<OutputDestInfo> outputs_;

        // Select UTXOs until we cover the requested amount
        std::vector<GeniusUTXO> selected_utxos;
        uint64_t                selected_amount = 0;

        std::unique_lock lock( utxos_mutex_ );
        auto            &utxo_list = utxos_[address_];
        for ( const auto &[state, utxo] : utxo_list )
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
            if ( state == UTXOState::UTXO_CONSUMED || state == UTXOState::UTXO_RESERVED )
            {
                continue;
            }
            inputs_.push_back( { utxo.GetTxID(), utxo.GetOutputIdx(), "" } );
            selected_utxos.push_back( utxo );
            selected_amount += utxo.GetAmount();
        }
        lock.unlock();

        // Abort if insufficient funds
        if ( selected_amount < amount || selected_utxos.empty() )
        {
            inputs_.clear();
            outputs_.clear();
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
        outputs_.reserve( 2 );

        // Create one output per TokenID for the destination

        outputs_.push_back( { send_totals, dest_address, token_id } );

        // Single change output (always from the last UTXO's token)
        uint64_t change = selected_amount - amount;
        if ( change > 0 )
        {
            outputs_.push_back( { change, address_, token_id } );
        }

        return std::make_pair( inputs_, outputs_ );
    }

    outcome::result<UTXOTxParameters> UTXOManager::CreateTxParameter( const std::vector<OutputDestInfo> &destinations,
                                                                      const TokenID                     &token_id )
    {
        std::vector<InputUTXOInfo>  inputs_;
        std::vector<OutputDestInfo> outputs_;

        uint64_t total_amount = 0;
        TokenID  change_token;

        for ( const auto &d : destinations )
        {
            total_amount += d.encrypted_amount;
        }

        uint64_t         used_amount = 0;
        std::unique_lock lock( utxos_mutex_ );
        for ( const auto &[state, utxo] : utxos_[address_] )
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
            if ( state == UTXOState::UTXO_CONSUMED || state == UTXOState::UTXO_RESERVED )
            {
                continue;
            }
            InputUTXOInfo curr_input{ utxo.GetTxID(), utxo.GetOutputIdx(), "" };
            used_amount += utxo.GetAmount();
            inputs_.push_back( curr_input );
            change_token = token_id;
        }
        lock.unlock();

        if ( used_amount < total_amount )
        {
            inputs_.clear();
            outputs_.clear();
        }
        else
        {
            outputs_ = destinations;
        }

        if ( used_amount > total_amount )
        {
            uint64_t change = used_amount - total_amount;
            outputs_.push_back( { change, address_, change_token } );
        }

        return std::make_pair( inputs_, outputs_ );
    }

    void UTXOManager::ReserveUTXOs( const std::vector<InputUTXOInfo> &inputs )
    {
        std::unique_lock lock( utxos_mutex_ );

        for ( auto &[state, utxo] : utxos_[address_] )
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

        for ( auto &[state, utxo] : utxos_[address_] )
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
}
