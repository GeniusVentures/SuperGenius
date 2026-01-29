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
            utxo_list.push_back( { UTXOState::UTXO_READY, std::move( new_utxo ) } );
        }
        return is_new;
    }

    void UTXOManager::DeleteUTXO( const base::Hash256 &utxo_id, const std::string &address )
    {
        // If not a full node and trying to delete UTXOs for other addresses, reject
        if ( !is_full_node_ && address != address_ )
        {
            logger_->warn( "Non-full node deleting UTXOs for other addresses" );
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

    bool UTXOManager::VerifyParameters( const UTXOTxParameters &params, const std::string &address ) const
    {
        size_t   input_amount    = 0;
        uint64_t expected_amount = 0;

        std::shared_lock lock( utxos_mutex_ );
        for ( const auto &[state, utxo] : utxos_.at( address ) )
        {
            for ( auto &input : params.first )
            {
                if ( state == UTXOState::UTXO_CONSUMED || state == UTXOState::UTXO_RESERVED )
                {
                    continue;
                }
                if ( input.txid_hash_ == utxo.GetTxID() )
                {
                    expected_amount += utxo.GetAmount();
                    input_amount    += 1;
                }
                if ( !verify_signature_( address, input.signature_, input.SerializeForSigning() ) )
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
        for ( const auto &[state, utxo] : utxos_[address_] )
        {
            if ( selected_amount >= required_amount )
            {
                break;
            }
            if ( utxo.GetLock() )
            {
                continue;
            }
            if ( state == UTXOState::UTXO_CONSUMED || state == UTXOState::UTXO_RESERVED )
            {
                continue;
            }
            if ( !token_id.Equals( utxo.GetTokenID() ) )
            {
                continue;
            }

            inputs.push_back( { utxo.GetTxID(), utxo.GetOutputIdx(), {} } );
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
