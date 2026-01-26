#include "UTXOManager.hpp"

#include <numeric>

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

    bool UTXOManager::PutUTXO( GeniusUTXO new_utxo, const std::string &address )
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
            utxo_list.push_back( std::move(new_utxo) );
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
        OUTCOME_TRY( auto selection_result, SelectUTXOs( amount, token_id ) );
        auto [inputs, selected_amount] = selection_result;

        std::vector<OutputDestInfo> outputs;
        // Reserve space: one output per token plus possible change
        outputs.reserve( 2 );

        // Primary output
        outputs.push_back( { amount, dest_address, token_id } );

        // Change output if needed
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
        uint64_t total_amount = 0;
        for ( const auto &d : destinations )
        {
            total_amount += d.encrypted_amount;
        }

        OUTCOME_TRY( auto selection_result, SelectUTXOs( total_amount, token_id ) );
        auto [inputs, selected_amount] = selection_result;

        std::vector<OutputDestInfo> outputs = destinations;

        // Change output if needed
        if ( selected_amount > total_amount )
        {
            uint64_t change = selected_amount - total_amount;
            outputs.push_back( { change, address_, token_id } );
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

    bool UTXOManager::VerifyParameters( const UTXOTxParameters &params ) const
    {
        size_t   input_amount    = 0;
        uint64_t expected_amount = 0;

        std::shared_lock lock( utxos_mutex_ );
        for ( const auto &utxo : utxos_.at( address_ ) )
        {
            for ( auto &input : params.first )
            {
                if ( input.txid_hash_ == utxo.GetTxID() )
                {
                    expected_amount += utxo.GetAmount();
                    input_amount    += 1;
                }
                if ( !verify_signature_( input.signature_, input.SerializeForSigning() ) )
                {
                    logger_->warn( "UTXO {} signing does not match", fmt::join( input.txid_hash_, "" ) );
                    return false;
                }
            }
        }
        lock.unlock();

        uint64_t real_amount = std::accumulate( params.second.cbegin(),
                                                params.second.cend(),
                                                UINT64_C( 0 ),
                                                []( const uint64_t s, const OutputDestInfo &o )
                                                { return o.encrypted_amount + s; } );

        return real_amount == expected_amount && input_amount == params.first.size();
    }

    outcome::result<std::pair<std::vector<InputUTXOInfo>, uint64_t>> UTXOManager::SelectUTXOs( uint64_t required_amount,
                                                                                               const TokenID &token_id )
    {
        std::vector<InputUTXOInfo> inputs;
        uint64_t                   selected_amount = 0;

        std::shared_lock lock( utxos_mutex_ );
        for ( const auto &utxo : utxos_[address_] )
        {
            if ( selected_amount >= required_amount )
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
            selected_amount += utxo.GetAmount();
        }
        lock.unlock();

        // Abort if insufficient funds
        if ( selected_amount < required_amount || inputs.empty() )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        return std::make_pair( inputs, selected_amount );
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
