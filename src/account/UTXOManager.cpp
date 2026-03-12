#include "UTXOManager.hpp"

#include <algorithm>
#include <numeric>
#include <stdexcept>

#include "account/proto/SGTransaction.pb.h"
#include "base/blob.hpp"
#include "crypto/sha/sha256.hpp"
#include "storage/database_error.hpp"

namespace sgns
{
    namespace
    {
        constexpr uint8_t kLeafPrefix = 0x00;
        constexpr uint8_t kNodePrefix = 0x01;

        void AppendUInt32BE( std::vector<uint8_t> &out, uint32_t value )
        {
            out.push_back( static_cast<uint8_t>( ( value >> 24 ) & 0xFF ) );
            out.push_back( static_cast<uint8_t>( ( value >> 16 ) & 0xFF ) );
            out.push_back( static_cast<uint8_t>( ( value >> 8 ) & 0xFF ) );
            out.push_back( static_cast<uint8_t>( value & 0xFF ) );
        }

        void AppendUInt64BE( std::vector<uint8_t> &out, uint64_t value )
        {
            out.push_back( static_cast<uint8_t>( ( value >> 56 ) & 0xFF ) );
            out.push_back( static_cast<uint8_t>( ( value >> 48 ) & 0xFF ) );
            out.push_back( static_cast<uint8_t>( ( value >> 40 ) & 0xFF ) );
            out.push_back( static_cast<uint8_t>( ( value >> 32 ) & 0xFF ) );
            out.push_back( static_cast<uint8_t>( ( value >> 24 ) & 0xFF ) );
            out.push_back( static_cast<uint8_t>( ( value >> 16 ) & 0xFF ) );
            out.push_back( static_cast<uint8_t>( ( value >> 8 ) & 0xFF ) );
            out.push_back( static_cast<uint8_t>( value & 0xFF ) );
        }

        base::Hash256 EmptyUTXOMerkleRoot()
        {
            static const base::Hash256 empty_root = crypto::sha256( std::string_view( "UTXO_EMPTY_V1" ) );
            return empty_root;
        }

        base::Hash256 HashLeaf( const std::vector<uint8_t> &payload )
        {
            std::vector<uint8_t> leaf_bytes;
            leaf_bytes.reserve( payload.size() + 1 );
            leaf_bytes.push_back( kLeafPrefix );
            leaf_bytes.insert( leaf_bytes.end(), payload.begin(), payload.end() );
            return crypto::sha256( gsl::span<const uint8_t>( leaf_bytes.data(), leaf_bytes.size() ) );
        }

        base::Hash256 HashNode( const base::Hash256 &left, const base::Hash256 &right )
        {
            std::vector<uint8_t> node_bytes;
            node_bytes.reserve( 1 + left.size() + right.size() );
            node_bytes.push_back( kNodePrefix );
            node_bytes.insert( node_bytes.end(), left.begin(), left.end() );
            node_bytes.insert( node_bytes.end(), right.begin(), right.end() );
            return crypto::sha256( gsl::span<const uint8_t>( node_bytes.data(), node_bytes.size() ) );
        }
    } // namespace

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
                utxo_list.erase( it );
                is_new = false;
                break;
            }
            //TODO - If it's the same, might be locked, then unlock
            is_new = false;
            break;
        }
        if ( is_new )
        {
            utxo_list.emplace_back( UTXOState::UTXO_READY, std::move( new_utxo ) );
            StoreUTXOs( address );
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
            bool  deleted   = false;
            auto &utxo_list = it->second;
            for ( auto utxo_it = utxo_list.begin(); utxo_it != utxo_list.end(); )
            {
                auto &[state, curr] = *utxo_it;
                if ( curr.GetTxID() == utxo_id )
                {
                    utxo_it = utxo_list.erase( utxo_it );
                    deleted = true;
                    continue;
                }
                ++utxo_it;
            }
            if ( deleted )
            {
                StoreUTXOs( address );
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

        StoreUTXOs( address );

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

    std::unordered_map<std::string, std::vector<UTXOManager::UTXOData>> UTXOManager::GetAllUTXOs() const
    {
        std::shared_lock lock( utxos_mutex_ );
        return utxos_;
    }

    outcome::result<void> UTXOManager::SetUTXOs( const std::vector<GeniusUTXO> &utxos, const std::string &address )
    {
        // If not a full node and trying to set UTXOs for other addresses, reject
        if ( !is_full_node_ && address != address_ )
        {
            logger_->warn( "Non-full node cannot set UTXOs for other addresses" );
            return std::errc::permission_denied;
        }

        std::unique_lock lock( utxos_mutex_ );
        auto            &utxo_list = utxos_[address];
        utxo_list.clear();
        utxo_list.reserve( utxos.size() );
        for ( const auto &utxo : utxos )
        {
            utxo_list.emplace_back( UTXOState::UTXO_READY, utxo );
        }

        if ( auto res = StoreUTXOs( address ); res.has_error() )
        {
            return res.error();
        }

        logger_->debug( "Set {} UTXOs for address {}", utxos.size(), address.substr( 0, 8 ) );
        return outcome::success();
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

        try
        {
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
        }
        catch ( const std::out_of_range & )
        {
            logger_->warn( "Could not find UTXOs from address {}", address );
            return false;
        }

        lock.unlock();

        uint64_t real_amount = std::accumulate( params.second.cbegin(),
                                                params.second.cend(),
                                                UINT64_C( 0 ),
                                                []( const uint64_t s, const OutputDestInfo &o )
                                                { return o.encrypted_amount + s; } );

        return real_amount == expected_amount && input_amount == params.first.size();
    }

    base::Hash256 UTXOManager::ComputeUTXOMerkleRoot() const
    {
        return ComputeUTXOMerkleRoot( address_ );
    }

    base::Hash256 UTXOManager::ComputeUTXOMerkleRoot( const std::string &address ) const
    {
        if ( !is_full_node_ && address != address_ )
        {
            logger_->warn( "Non-full node cannot compute UTXO Merkle root for other addresses" );
            return EmptyUTXOMerkleRoot();
        }

        std::vector<GeniusUTXO> unspent;
        {
            std::shared_lock lock( utxos_mutex_ );
            auto             it = utxos_.find( address );
            if ( it == utxos_.end() )
            {
                return EmptyUTXOMerkleRoot();
            }

            unspent.reserve( it->second.size() );
            for ( const auto &[state, utxo] : it->second )
            {
                if ( state != UTXOState::UTXO_READY )
                {
                    continue;
                }
                unspent.push_back( utxo );
            }
        }

        if ( unspent.empty() )
        {
            return EmptyUTXOMerkleRoot();
        }

        std::sort( unspent.begin(),
                   unspent.end(),
                   []( const GeniusUTXO &lhs, const GeniusUTXO &rhs )
                   {
                       if ( lhs.GetTxID() != rhs.GetTxID() )
                       {
                           return lhs.GetTxID() < rhs.GetTxID();
                       }
                       return lhs.GetOutputIdx() < rhs.GetOutputIdx();
                   } );

        std::vector<base::Hash256> level_hashes;
        level_hashes.reserve( unspent.size() );

        for ( const auto &utxo : unspent )
        {
            std::vector<uint8_t> payload;
            payload.reserve( 32 + 4 + 4 + address.size() + utxo.GetTokenID().bytes().size() + 8 );

            payload.insert( payload.end(), utxo.GetTxID().begin(), utxo.GetTxID().end() );
            AppendUInt32BE( payload, utxo.GetOutputIdx() );
            AppendUInt32BE( payload, static_cast<uint32_t>( address.size() ) );
            payload.insert( payload.end(), address.begin(), address.end() );

            const auto &token_bytes = utxo.GetTokenID().bytes();
            payload.insert( payload.end(), token_bytes.begin(), token_bytes.end() );
            AppendUInt64BE( payload, utxo.GetAmount() );

            level_hashes.push_back( HashLeaf( payload ) );
        }

        while ( level_hashes.size() > 1 )
        {
            if ( ( level_hashes.size() % 2 ) != 0 )
            {
                level_hashes.push_back( level_hashes.back() );
            }

            std::vector<base::Hash256> next_level;
            next_level.reserve( level_hashes.size() / 2 );
            for ( size_t i = 0; i < level_hashes.size(); i += 2 )
            {
                next_level.push_back( HashNode( level_hashes[i], level_hashes[i + 1] ) );
            }
            level_hashes = std::move( next_level );
        }

        return level_hashes.front();
    }

    outcome::result<bool> UTXOManager::LoadUTXOs( std::shared_ptr<storage::rocksdb> db )
    {
        if ( db == nullptr )
        {
            logger_->error( "Tried to initialize DB with null pointer" );
            return std::errc::invalid_argument;
        }

        if ( db_ != nullptr )
        {
            logger_->warn( "UTXOs were already loaded" );
        }
        db_ = std::move( db );
        utxos_.clear();

        base::Buffer key_buf;
        key_buf.put( DB_PREFIX );
        auto utxo_list = db_->query( key_buf );

        if ( utxo_list.has_error() )
        {
            if ( utxo_list.error() == storage::DatabaseError::NOT_FOUND )
            {
                logger_->info( "Unable to find UTXOs in storage" );
                return false;
            }
            logger_->error( "Failed to get UTXO list: {}", utxo_list.error().message() );
            return utxo_list.error();
        }

        if ( utxo_list.value().size() == 0 )
        {
            logger_->warn( "Found UTXOs in storage, but there were none" );
            return false;
        }

        for ( const auto &[key, params] : utxo_list.value() )
        {
            std::string address( key.subbuffer( DB_PREFIX.size() + 1 ).toString() );
            logger_->info( "Loading UTXOs of address {}", address );

            SGTransaction::UTXOList utxos;

            if ( !utxos.ParseFromArray( params.data(), params.size() ) )
            {
                logger_->error( "Failed to deserialize UTXOs" );
                return std::errc::bad_message;
            }

            utxos_[address].reserve( utxos.utxos_size() );

            for ( int i = 0; i < utxos.utxos_size(); ++i )
            {
                const auto &utxo = utxos.utxos( i );
                OUTCOME_TRY( auto hash,
                             base::Hash256::fromSpan(
                                 gsl::span( reinterpret_cast<uint8_t *>( const_cast<char *>( utxo.hash().data() ) ),
                                            utxo.hash().size() ) ) );

                auto token_id = TokenID::FromBytes( utxo.token().data(), utxo.token().size() );

                utxos_[address].emplace_back( UTXOState::UTXO_READY,
                                              GeniusUTXO( hash, utxo.output_idx(), utxo.amount(), token_id ) );
            }
        }

        return true;
    }

    outcome::result<void> UTXOManager::StoreUTXOs( const std::string &address )
    {
        if ( db_ == nullptr )
        {
            logger_->error( "Tried to store UTXOs without loading DB" );
            return storage::DatabaseError::UNITIALIZED;
        }

        SGTransaction::UTXOList utxos;

        try
        {
            for ( const auto &[state, utxo] : utxos_.at( address ) )
            {
                if ( state != UTXOState::UTXO_READY )
                {
                    continue;
                }
                auto new_utxo = utxos.add_utxos();
                new_utxo->set_hash( utxo.GetTxID().data(), utxo.GetTxID().size() );
                new_utxo->set_token( utxo.GetTokenID().bytes().data(), utxo.GetTokenID().size() );
                new_utxo->set_amount( utxo.GetAmount() );
                new_utxo->set_output_idx( utxo.GetOutputIdx() );
            }
        }
        catch ( const std::out_of_range & )
        {
            logger_->error( "There are no UTXOs in cache for address {}", address );
            return std::errc::bad_address;
        }

        base::Buffer buf( std::vector<uint8_t>( utxos.ByteSizeLong() ) );
        if ( !utxos.SerializeToArray( buf.data(), buf.size() ) )
        {
            logger_->error( "Failed to serialize to array" );
            return std::errc::bad_message;
        }

        std::string key( DB_PREFIX );
        key.push_back( '/' );
        key.append( address );
        base::Buffer key_buf;
        key_buf.put( key );

        if ( auto result = db_->put( key_buf, buf ); result.has_error() )
        {
            logger_->error( "Error when storing UTXOs" );
            return result.error();
        }

        logger_->info( "Stored {} UTXOs for address {}", utxos.utxos_size(), address );
        return outcome::success();
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
