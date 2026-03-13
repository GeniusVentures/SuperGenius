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

        void RemoveOutPointFromVector( std::vector<OutPoint> &outpoints, const OutPoint &target )
        {
            outpoints.erase( std::remove( outpoints.begin(), outpoints.end(), target ), outpoints.end() );
        }

        std::string BuildUTXORecordKey( const std::string &owner_address, const OutPoint &outpoint )
        {
            return fmt::format( "/utxo/{}/{}:{}", owner_address, outpoint.txid_hash_.toReadableString(), outpoint.output_idx_ );
        }

        std::optional<std::string> ParseOwnerAddrFromUTXORecordKey( std::string_view key )
        {
            constexpr std::string_view prefix = "/utxo/";
            if ( key.substr( 0, prefix.size() ) != prefix )
            {
                return std::nullopt;
            }

            auto remainder = key.substr( prefix.size() );
            auto slash_pos = remainder.find( '/' );
            if ( slash_pos == std::string_view::npos || slash_pos == 0 )
            {
                return std::nullopt;
            }

            return std::string( remainder.substr( 0, slash_pos ) );
        }

        SGTransaction::UTXOEntryState ToProtoState( UTXOManager::UTXOState state )
        {
            return state == UTXOManager::UTXOState::UTXO_CONSUMED ? SGTransaction::UTXO_ENTRY_CONSUMED
                                                                  : SGTransaction::UTXO_ENTRY_READY;
        }

        UTXOManager::UTXOState FromProtoState( SGTransaction::UTXOEntryState state )
        {
            return state == SGTransaction::UTXO_ENTRY_CONSUMED ? UTXOManager::UTXOState::UTXO_CONSUMED
                                                                : UTXOManager::UTXOState::UTXO_READY;
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
        if ( auto address_it = address_outpoints_.find( address ); address_it != address_outpoints_.end() )
        {
            for ( const auto &outpoint : address_it->second )
            {
                auto utxo_it = utxo_outpoints_.find( outpoint );
                if ( utxo_it == utxo_outpoints_.end() )
                {
                    continue;
                }
                if ( utxo_it->second.state != UTXOState::UTXO_READY )
                {
                    continue;
                }
                if ( reserved_outpoints_.find( outpoint ) == reserved_outpoints_.end() )
                {
                    //TODO - This should return in Genius Tokens but it's not taking into consideration the tokenID. It needs to multiply by the ratio of it
                    retval += utxo_it->second.utxo.GetAmount();
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
        if ( auto address_it = address_outpoints_.find( address ); address_it != address_outpoints_.end() )
        {
            for ( const auto &outpoint : address_it->second )
            {
                auto utxo_it = utxo_outpoints_.find( outpoint );
                if ( utxo_it == utxo_outpoints_.end() )
                {
                    continue;
                }
                if ( utxo_it->second.state != UTXOState::UTXO_READY )
                {
                    continue;
                }
                if ( !token_id.Equals( utxo_it->second.utxo.GetTokenID() ) )
                {
                    continue;
                }
                if ( reserved_outpoints_.find( outpoint ) == reserved_outpoints_.end() )
                {
                    balance += utxo_it->second.utxo.GetAmount();
                }
            }
        }
        return balance;
    }

    //TODO - Remove the GeniusUTXO from parameters, instead add the necessary fields or IGeniusTransactions
    bool UTXOManager::PutUTXO( GeniusUTXO new_utxo, const std::string &address )
    {
        // If not a full node and trying to store UTXOs for other addresses, reject
        if ( !is_full_node_ && address != address_ )
        {
            logger_->debug( "Non-full node cannot store UTXOs for other addresses" );
            return false;
        }

        new_utxo.SetOwnerAddress( address );
        const OutPoint outpoint{ new_utxo.GetTxID(), new_utxo.GetOutputIdx() };

        std::unique_lock lock( utxos_mutex_ );
        if ( auto existing = utxo_outpoints_.find( outpoint ); existing != utxo_outpoints_.end() )
        {
            return false;
        }

        utxo_outpoints_[outpoint] = UTXOEntry{ UTXOState::UTXO_READY, new_utxo };
        address_outpoints_[address].push_back( outpoint );

        StoreUTXOs( address );
        return true;
    }

    void UTXOManager::DeleteUTXO( const base::Hash256 &utxo_id, uint32_t output_idx, const std::string &address )
    {
        // If not a full node and trying to delete UTXOs for other addresses, reject
        if ( !is_full_node_ && address != address_ )
        {
            logger_->warn( "Non-full node deleting UTXOs for other addresses" );
        }

        std::unique_lock lock( utxos_mutex_ );
        if ( auto address_it = address_outpoints_.find( address ); address_it != address_outpoints_.end() )
        {
            auto &outpoints = address_it->second;
            auto  outpoint_it =
                std::find_if( outpoints.begin(),
                              outpoints.end(),
                              [&]( const OutPoint &outpoint )
                              { return outpoint.txid_hash_ == utxo_id && outpoint.output_idx_ == output_idx; } );
            if ( outpoint_it != outpoints.end() )
            {
                const OutPoint outpoint = *outpoint_it;
                reserved_outpoints_.erase( outpoint );
                utxo_outpoints_.erase( outpoint );
                outpoints.erase( outpoint_it );
                StoreUTXOs( address );
            }
        }
    }

    bool UTXOManager::ConsumeUTXOs( const std::vector<InputUTXOInfo> &infos, const std::string &address )
    {
        bool             consumed = true;
        std::unique_lock lock( utxos_mutex_ );
        for ( auto &input_info : infos )
        {
            const OutPoint outpoint{ input_info.txid_hash_, input_info.output_idx_ };
            bool           utxo_found = false;

            if ( auto canonical_it = utxo_outpoints_.find( outpoint ); canonical_it != utxo_outpoints_.end() )
            {
                auto &entry = canonical_it->second;
                if ( entry.state == UTXOState::UTXO_READY && entry.utxo.GetOwnerAddress() == address )
                {
                    utxo_found   = true;
                    entry.state  = UTXOState::UTXO_CONSUMED;
                    entry.utxo.SetOwnerAddress( address );
                }
            }

            reserved_outpoints_.erase( outpoint );
            if ( auto address_it = address_outpoints_.find( address ); address_it != address_outpoints_.end() )
            {
                RemoveOutPointFromVector( address_it->second, outpoint );
            }

            GeniusUTXO consumed_utxo( input_info.txid_hash_, input_info.output_idx_, 0, TokenID(), address );
            utxo_outpoints_[outpoint] = UTXOEntry{ UTXOState::UTXO_CONSUMED, consumed_utxo };

            consumed = consumed && utxo_found;
        }

        StoreUTXOs( address );

        return consumed;
    }

    std::vector<GeniusUTXO> UTXOManager::GetUTXOs( const std::string &address ) const
    {
        std::shared_lock lock( utxos_mutex_ );
        if ( auto address_it = address_outpoints_.find( address ); address_it != address_outpoints_.end() )
        {
            std::vector<GeniusUTXO> result;
            result.reserve( address_it->second.size() );
            for ( const auto &outpoint : address_it->second )
            {
                auto utxo_it = utxo_outpoints_.find( outpoint );
                if ( utxo_it == utxo_outpoints_.end() )
                {
                    continue;
                }
                if ( utxo_it->second.state != UTXOState::UTXO_READY )
                {
                    continue;
                }
                result.push_back( utxo_it->second.utxo );
            }
            return result;
        }
        return {};
    }

    std::unordered_map<std::string, std::vector<UTXOManager::UTXOData>> UTXOManager::GetAllUTXOs() const
    {
        std::shared_lock lock( utxos_mutex_ );
        std::unordered_map<std::string, std::vector<UTXOData>> result;
        for ( const auto &[outpoint, entry] : utxo_outpoints_ )
        {
            (void)outpoint;
            const auto &owner = entry.utxo.GetOwnerAddress();
            result[owner].emplace_back( entry.state, entry.utxo );
        }
        return result;
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

        if ( auto address_it = address_outpoints_.find( address ); address_it != address_outpoints_.end() )
        {
            for ( const auto &outpoint : address_it->second )
            {
                utxo_outpoints_.erase( outpoint );
                reserved_outpoints_.erase( outpoint );
            }
            address_it->second.clear();
        }

        auto &outpoints = address_outpoints_[address];
        outpoints.clear();
        outpoints.reserve( utxos.size() );
        for ( const auto &utxo : utxos )
        {
            auto owned_utxo = utxo;
            owned_utxo.SetOwnerAddress( address );
            const OutPoint outpoint{ owned_utxo.GetTxID(), owned_utxo.GetOutputIdx() };
            utxo_outpoints_[outpoint] = UTXOEntry{ UTXOState::UTXO_READY, owned_utxo };
            outpoints.push_back( outpoint );
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

        for ( const auto &input_utxo : inputs )
        {
            reserved_outpoints_.insert( OutPoint{ input_utxo.txid_hash_, input_utxo.output_idx_ } );
        }
    }

    void UTXOManager::RollbackUTXOs( const std::vector<InputUTXOInfo> &inputs )
    {
        std::unique_lock lock( utxos_mutex_ );

        for ( const auto &input_utxo : inputs )
        {
            reserved_outpoints_.erase( OutPoint{ input_utxo.txid_hash_, input_utxo.output_idx_ } );
        }
    }

    bool UTXOManager::VerifyParameters( const UTXOTxParameters &params, const std::string &address ) const
    {
        uint64_t expected_amount = 0;

        std::shared_lock lock( utxos_mutex_ );

        if ( address_outpoints_.find( address ) == address_outpoints_.end() )
        {
            logger_->warn( "Could not find UTXOs from address {}", address );
            return false;
        }

        std::unordered_set<OutPoint, OutPointHash> seen_inputs;
        seen_inputs.reserve( params.first.size() );

        for ( const auto &input : params.first )
        {
            if ( !verify_signature_( address, input.signature_, input.SerializeForSigning() ) )
            {
                logger_->warn( "UTXO {} signing does not match", fmt::join( input.txid_hash_, "" ) );
                return false;
            }

            const OutPoint outpoint{ input.txid_hash_, input.output_idx_ };
            if ( !seen_inputs.insert( outpoint ).second )
            {
                logger_->warn( "Duplicate input outpoint detected for {}", input.txid_hash_.toReadableString() );
                return false;
            }

            auto utxo_it = utxo_outpoints_.find( outpoint );
            if ( utxo_it == utxo_outpoints_.end() )
            {
                logger_->warn( "Unknown outpoint {}:{}", input.txid_hash_.toReadableString(), input.output_idx_ );
                return false;
            }

            if ( utxo_it->second.state != UTXOState::UTXO_READY )
            {
                logger_->warn( "Outpoint {}:{} is not spendable", input.txid_hash_.toReadableString(), input.output_idx_ );
                return false;
            }

            if ( utxo_it->second.utxo.GetOwnerAddress() != address )
            {
                logger_->warn( "Outpoint {}:{} does not belong to {}", input.txid_hash_.toReadableString(), input.output_idx_, address );
                return false;
            }

            expected_amount += utxo_it->second.utxo.GetAmount();
        }

        uint64_t real_amount = std::accumulate( params.second.cbegin(),
                                                params.second.cend(),
                                                UINT64_C( 0 ),
                                                []( const uint64_t s, const OutputDestInfo &o )
                                                { return o.encrypted_amount + s; } );

        return real_amount == expected_amount && seen_inputs.size() == params.first.size();
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
            auto             it = address_outpoints_.find( address );
            if ( it == address_outpoints_.end() )
            {
                return EmptyUTXOMerkleRoot();
            }

            unspent.reserve( it->second.size() );
            for ( const auto &outpoint : it->second )
            {
                auto utxo_it = utxo_outpoints_.find( outpoint );
                if ( utxo_it == utxo_outpoints_.end() )
                {
                    continue;
                }
                if ( utxo_it->second.state != UTXOState::UTXO_READY )
                {
                    continue;
                }
                unspent.push_back( utxo_it->second.utxo );
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
            const auto &owner_address = utxo.GetOwnerAddress();
            payload.reserve( 32 + 4 + 4 + owner_address.size() + utxo.GetTokenID().bytes().size() + 8 );

            payload.insert( payload.end(), utxo.GetTxID().begin(), utxo.GetTxID().end() );
            AppendUInt32BE( payload, utxo.GetOutputIdx() );
            AppendUInt32BE( payload, static_cast<uint32_t>( owner_address.size() ) );
            payload.insert( payload.end(), owner_address.begin(), owner_address.end() );

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
        utxo_outpoints_.clear();
        address_outpoints_.clear();
        reserved_outpoints_.clear();

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
            auto owner_addr_opt = ParseOwnerAddrFromUTXORecordKey( key.toString() );
            if ( !owner_addr_opt.has_value() )
            {
                logger_->warn( "Skipping malformed UTXO key {}", key.toString() );
                continue;
            }
            const auto &address = owner_addr_opt.value();

            SGTransaction::UTXOEntryRecord entry_record;
            if ( !entry_record.ParseFromArray( params.data(), params.size() ) )
            {
                logger_->error( "Failed to deserialize UTXO record for address {}", address );
                return std::errc::bad_message;
            }

            if ( !entry_record.owner_address().empty() && entry_record.owner_address() != address )
            {
                logger_->warn( "UTXO owner mismatch in key/value for {}", address );
            }

            const auto state = FromProtoState( entry_record.state() );

            OUTCOME_TRY( auto hash,
                         base::Hash256::fromSpan(
                             gsl::span( reinterpret_cast<uint8_t *>( const_cast<char *>( entry_record.utxo().hash().data() ) ),
                                        entry_record.utxo().hash().size() ) ) );

            auto      token_id = TokenID::FromBytes( entry_record.utxo().token().data(), entry_record.utxo().token().size() );
            GeniusUTXO loaded_utxo( hash,
                                    entry_record.utxo().output_idx(),
                                    entry_record.utxo().amount(),
                                    token_id,
                                    address );
            const auto outpoint = loaded_utxo.GetOutPoint();
            UTXOEntry loaded_entry;
            loaded_entry.state         = state;
            loaded_entry.utxo          = loaded_utxo;
            loaded_entry.created_epoch = entry_record.created_epoch();
            if ( entry_record.has_spent_epoch() )
            {
                loaded_entry.spent_epoch = entry_record.spent_epoch();
            }
            if ( entry_record.has_spent_by_txid() )
            {
                OUTCOME_TRY( auto spent_by_hash,
                             base::Hash256::fromSpan( gsl::span(
                                 reinterpret_cast<uint8_t *>( const_cast<char *>( entry_record.spent_by_txid().data() ) ),
                                 entry_record.spent_by_txid().size() ) ) );
                loaded_entry.spent_by_txid = spent_by_hash;
            }

            utxo_outpoints_[outpoint] = std::move( loaded_entry );
            address_outpoints_[address].push_back( outpoint );
        }

        return !utxo_outpoints_.empty();
    }

    outcome::result<void> UTXOManager::StoreUTXOs( const std::string &address )
    {
        if ( db_ == nullptr )
        {
            logger_->error( "Tried to store UTXOs without loading DB" );
            return storage::DatabaseError::UNITIALIZED;
        }

        base::Buffer existing_prefix;
        existing_prefix.put( fmt::format( "{}/{}/", DB_PREFIX, address ) );

        auto existing_records = db_->query( existing_prefix );
        if ( existing_records.has_error() && existing_records.error() != storage::DatabaseError::NOT_FOUND )
        {
            logger_->error( "Failed to query existing UTXO records for address {}", address );
            return existing_records.error();
        }

        if ( existing_records.has_value() )
        {
            //TODO - not great because it's not atomic, so we lose the record and if we shutdown before we record it is gone.
            for ( const auto &[existing_key, _] : existing_records.value() )
            {
                if ( auto rem_res = db_->remove( existing_key ); rem_res.has_error() )
                {
                    logger_->error( "Failed to remove old UTXO record for address {}", address );
                    return rem_res.error();
                }
            }
        }

        uint64_t stored = 0;
        for ( const auto &[outpoint, entry] : utxo_outpoints_ )
        {
            if ( entry.utxo.GetOwnerAddress() != address )
            {
                continue;
            }

            SGTransaction::UTXOEntryRecord entry_record;
            auto                          *utxo_proto = entry_record.mutable_utxo();
            utxo_proto->set_hash( entry.utxo.GetTxID().data(), entry.utxo.GetTxID().size() );
            utxo_proto->set_token( entry.utxo.GetTokenID().bytes().data(), entry.utxo.GetTokenID().size() );
            utxo_proto->set_amount( entry.utxo.GetAmount() );
            utxo_proto->set_output_idx( entry.utxo.GetOutputIdx() );
            entry_record.set_owner_address( address );
            entry_record.set_state( ToProtoState( entry.state ) );
            entry_record.set_created_epoch( entry.created_epoch );
            entry_record.set_has_spent_epoch( entry.spent_epoch.has_value() );
            if ( entry.spent_epoch.has_value() )
            {
                entry_record.set_spent_epoch( entry.spent_epoch.value() );
            }
            entry_record.set_has_spent_by_txid( entry.spent_by_txid.has_value() );
            if ( entry.spent_by_txid.has_value() )
            {
                entry_record.set_spent_by_txid( entry.spent_by_txid.value().data(), entry.spent_by_txid.value().size() );
            }

            base::Buffer value_buf( std::vector<uint8_t>( entry_record.ByteSizeLong() ) );
            if ( !entry_record.SerializeToArray( value_buf.data(), value_buf.size() ) )
            {
                logger_->error( "Failed to serialize UTXO record for address {}", address );
                return std::errc::bad_message;
            }

            base::Buffer key_buf;
            key_buf.put( BuildUTXORecordKey( address, outpoint ) );

            if ( auto put_res = db_->put( key_buf, value_buf ); put_res.has_error() )
            {
                logger_->error( "Error when storing UTXO record for address {}", address );
                return put_res.error();
            }
            ++stored;
        }

        logger_->info( "Stored {} UTXOs for address {}", stored, address );
        return outcome::success();
    }

    outcome::result<std::pair<std::vector<InputUTXOInfo>, uint64_t>> UTXOManager::SelectUTXOs( uint64_t required_amount,
                                                                                               const TokenID &token_id )
    {
        std::vector<InputUTXOInfo> inputs;
        uint64_t                   selected_amount = 0;

        std::shared_lock lock( utxos_mutex_ );
        if ( auto address_it = address_outpoints_.find( address_ ); address_it != address_outpoints_.end() )
        {
            for ( const auto &outpoint : address_it->second )
            {
                if ( selected_amount >= required_amount )
                {
                    break;
                }

                auto utxo_it = utxo_outpoints_.find( outpoint );
                if ( utxo_it == utxo_outpoints_.end() )
                {
                    continue;
                }
                const auto &entry = utxo_it->second;
                if ( entry.state != UTXOState::UTXO_READY )
                {
                    continue;
                }
                if ( reserved_outpoints_.find( outpoint ) != reserved_outpoints_.end() )
                {
                    continue;
                }
                if ( !token_id.Equals( entry.utxo.GetTokenID() ) )
                {
                    continue;
                }

                inputs.push_back( { entry.utxo.GetTxID(), entry.utxo.GetOutputIdx(), {} } );
                selected_amount += entry.utxo.GetAmount();
            }
        }

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
