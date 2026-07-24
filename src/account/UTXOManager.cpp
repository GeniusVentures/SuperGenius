#include "UTXOManager.hpp"
#include "UTXOMerkle.hpp"

#include <algorithm>
#include <chrono>
#include <numeric>
#include <stdexcept>

#include "account/proto/SGTransaction.pb.h"
#include "base/blob.hpp"
#include "storage/database_error.hpp"

namespace sgns
{
    namespace
    {

        std::string BuildUTXORecordKey( const std::string &owner_address, const OutPoint &outpoint )
        {
            return fmt::format( "/utxo/{}/{}:{}",
                                owner_address,
                                outpoint.txid_hash_.toReadableString(),
                                outpoint.output_idx_ );
        }

        std::string BuildCheckpointRecordKey( const std::string &owner_address, uint64_t epoch )
        {
            return fmt::format( "/utxo-checkpoint/{}/{}", owner_address, epoch );
        }

        std::string BuildLatestCheckpointPointerKey( const std::string &owner_address )
        {
            return fmt::format( "/utxo-checkpoint/{}/latest", owner_address );
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
            switch ( state )
            {
                case UTXOManager::UTXOState::UTXO_RESERVED:
                    return SGTransaction::UTXO_ENTRY_RESERVED;
                case UTXOManager::UTXOState::UTXO_CONSUMED:
                    return SGTransaction::UTXO_ENTRY_CONSUMED;
                default:
                    return SGTransaction::UTXO_ENTRY_READY;
            }
        }

        UTXOManager::UTXOState FromProtoState( SGTransaction::UTXOEntryState state )
        {
            switch ( state )
            {
                case SGTransaction::UTXO_ENTRY_RESERVED:
                    return UTXOManager::UTXOState::UTXO_RESERVED;
                case SGTransaction::UTXO_ENTRY_CONSUMED:
                    return UTXOManager::UTXOState::UTXO_CONSUMED;
                default:
                    return UTXOManager::UTXOState::UTXO_READY;
            }
        }

        SGTransaction::UTXOEntryType ToProtoType( UTXOManager::UTXOType type )
        {
            return type == UTXOManager::UTXOType::UTXO_BRIDGE
                     ? SGTransaction::UTXO_ENTRY_BRIDGE
                     : SGTransaction::UTXO_ENTRY_NORMAL;
        }

        UTXOManager::UTXOType FromProtoType( SGTransaction::UTXOEntryType type )
        {
            return type == SGTransaction::UTXO_ENTRY_BRIDGE
                     ? UTXOManager::UTXOType::UTXO_BRIDGE
                     : UTXOManager::UTXOType::UTXO_NORMAL;
        }

        outcome::result<base::Buffer> SerializeUTXOEntry( const UTXOManager::UTXOEntry &entry,
                                                          const std::string &address )
        {
            SGTransaction::UTXOEntryRecord record;
            auto *utxo = record.mutable_utxo();
            const auto txid = entry.utxo.GetTxID();
            const auto token = entry.utxo.GetTokenID();
            utxo->set_hash( txid.data(), txid.size() );
            utxo->set_token( token.bytes().data(), token.size() );
            utxo->set_amount( entry.utxo.GetAmount() );
            utxo->set_output_idx( entry.utxo.GetOutputIdx() );
            record.set_owner_address( address );
            record.set_state( ToProtoState( entry.state ) );
            record.set_type( ToProtoType( entry.type ) );
            record.set_created_epoch( entry.created_epoch );
            record.set_has_spent_epoch( entry.spent_epoch.has_value() );
            if ( entry.spent_epoch )
            {
                record.set_spent_epoch( *entry.spent_epoch );
            }
            record.set_has_spent_by_txid( entry.spent_by_txid.has_value() );
            if ( entry.spent_by_txid )
            {
                record.set_spent_by_txid( entry.spent_by_txid->data(), entry.spent_by_txid->size() );
            }
            base::Buffer value( std::vector<uint8_t>( record.ByteSizeLong() ) );
            if ( !record.SerializeToArray( value.data(), value.size() ) )
            {
                return outcome::failure( std::errc::bad_message );
            }
            return value;
        }

        base::Hash256 ComputeMerkleRootFromUTXOList( std::vector<GeniusUTXO> unspent )
        {
            return utxo_merkle::ComputeMerkleRootFromUTXOs( unspent );
        }

    } // namespace

    uint64_t UTXOManager::GetBalance() const
    {
        return GetBalance( address_ );
    }

    uint64_t UTXOManager::GetBalance( const std::string &address ) const
    {
        uint64_t retval = 0;

        // D-17: foreign-address guard removed — all nodes track UTXOs for all peers

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
                //TODO - This should return in Genius Tokens but it's not taking into consideration the tokenID. It needs to multiply by the ratio of it
                retval += utxo_it->second.utxo.GetAmount();
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

        // D-17: foreign-address guard removed — all nodes track UTXOs for all peers

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
                balance += utxo_it->second.utxo.GetAmount();
            }
        }
        return balance;
    }

    //TODO - Remove the GeniusUTXO from parameters, instead add the necessary fields or GeniusTransaction
    outcome::result<bool> UTXOManager::PutUTXO( GeniusUTXO            new_utxo,
                                                const std::string    &address,
                                                UTXOManager::UTXOType type )
    {
        // D-17: foreign-address guard removed — all nodes store UTXOs for all peers

        new_utxo.SetOwnerAddress( address );
        const OutPoint outpoint{ new_utxo.GetTxID(), new_utxo.GetOutputIdx() };

        {
            std::unique_lock lock( utxos_mutex_ );
            if ( auto existing = utxo_outpoints_.find( outpoint ); existing != utxo_outpoints_.end() )
            {
                return false;
            }

            UTXOEntry entry;
            entry.state               = UTXOState::UTXO_READY;
            entry.utxo                = new_utxo;
            entry.created_epoch       = 0;
            entry.spent_epoch         = std::nullopt;
            entry.spent_by_txid       = std::nullopt;
            entry.type                = type;
            utxo_outpoints_[outpoint] = entry;
            address_outpoints_[address].push_back( outpoint );
        }

        BOOST_OUTCOME_TRY( StoreUTXOs( address ) );
        return true;
    }

    outcome::result<void> UTXOManager::DeleteUTXO( const base::Hash256 &utxo_id,
                                                   uint32_t             output_idx,
                                                   const std::string   &address )
    {
        // D-17: foreign-address guard removed — all nodes manage UTXOs for all peers

        {
            std::unique_lock lock( utxos_mutex_ );
            if ( auto address_it = address_outpoints_.find( address ); address_it != address_outpoints_.end() )
            {
                auto &outpoints   = address_it->second;
                auto  outpoint_it = std::find_if(
                    outpoints.begin(),
                    outpoints.end(),
                    [&]( const OutPoint &outpoint )
                    { return outpoint.txid_hash_ == utxo_id && outpoint.output_idx_ == output_idx; } );
                if ( outpoint_it != outpoints.end() )
                {
                    const OutPoint outpoint = *outpoint_it;
                    local_reservations_.erase( outpoint );
                    utxo_outpoints_.erase( outpoint );
                    outpoints.erase( outpoint_it );
                }
            }
        }

        BOOST_OUTCOME_TRY( StoreUTXOs( address ) );
        return outcome::success();
    }

    outcome::result<bool> UTXOManager::ConsumeUTXOs( const std::vector<InputUTXOInfo> &infos,
                                                     const std::string                &address,
                                                     UTXOType                          type )
    {
        bool consumed = true;
        {
            std::unique_lock lock( utxos_mutex_ );
            for ( auto &input_info : infos )
            {
                const OutPoint outpoint{ input_info.txid_hash_, input_info.output_idx_ };
                bool           utxo_found = false;
                std::string    stored_owner;

                if ( auto canonical_it = utxo_outpoints_.find( outpoint ); canonical_it != utxo_outpoints_.end() )
                {
                    auto &entry              = canonical_it->second;
                    stored_owner             = entry.utxo.GetOwnerAddress();
                    const bool owner_matches = entry.type == UTXOType::UTXO_BRIDGE || stored_owner == address;
                    if ( ( entry.state == UTXOState::UTXO_READY || entry.state == UTXOState::UTXO_RESERVED ) &&
                         owner_matches && entry.type == type )
                    {
                        utxo_found  = true;
                        entry.state = UTXOState::UTXO_CONSUMED;
                    }
                }

                const auto &indexed_owner = stored_owner.empty() ? address : stored_owner;
                if ( auto address_it = address_outpoints_.find( indexed_owner );
                     address_it != address_outpoints_.end() )
                {
                    auto &outpoints_vector = address_it->second;
                    outpoints_vector.erase( std::remove( outpoints_vector.begin(), outpoints_vector.end(), outpoint ),
                                            outpoints_vector.end() );
                }

                local_reservations_.erase( outpoint );
                if ( !utxo_found )
                {
                    GeniusUTXO consumed_utxo( input_info.txid_hash_, input_info.output_idx_, 0, TokenID(), address );
                    utxo_outpoints_[outpoint] = UTXOEntry{ UTXOState::UTXO_CONSUMED,
                                                           consumed_utxo,
                                                           0,
                                                           std::nullopt,
                                                           std::nullopt,
                                                           type };
                }

                consumed = consumed && utxo_found;
            }
        }

        BOOST_OUTCOME_TRY( StoreUTXOs( address ) );

        return consumed;
    }

    outcome::result<void> UTXOManager::RestoreConsumedUTXOs( const std::vector<InputUTXOInfo> &infos,
                                                             const std::string                &address,
                                                             UTXOType                          type )
    {
        {
            std::unique_lock lock( utxos_mutex_ );
            for ( const auto &input : infos )
            {
                const OutPoint outpoint{ input.txid_hash_, input.output_idx_ };
                auto           entry_it = utxo_outpoints_.find( outpoint );
                if ( entry_it == utxo_outpoints_.end() || entry_it->second.state != UTXOState::UTXO_CONSUMED ||
                     entry_it->second.type != type || entry_it->second.utxo.GetOwnerAddress() != address )
                {
                    return outcome::failure( std::errc::invalid_argument );
                }
            }

            for ( const auto &input : infos )
            {
                const OutPoint outpoint{ input.txid_hash_, input.output_idx_ };
                auto          &entry = utxo_outpoints_.at( outpoint );
                entry.state          = UTXOState::UTXO_READY;
                entry.spent_epoch.reset();
                entry.spent_by_txid.reset();
                local_reservations_.erase( outpoint );

                auto &owner_outpoints = address_outpoints_[address];
                if ( std::find( owner_outpoints.begin(), owner_outpoints.end(), outpoint ) == owner_outpoints.end() )
                {
                    owner_outpoints.push_back( outpoint );
                }
            }
        }

        if ( !infos.empty() )
        {
            BOOST_OUTCOME_TRY( StoreUTXOs( address ) );
        }

        return outcome::success();
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

    std::vector<GeniusUTXO> UTXOManager::GetUnconsumedUTXOs( const std::string &address ) const
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
                const auto &entry = utxo_it->second;
                if ( entry.state == UTXOState::UTXO_CONSUMED )
                {
                    continue;
                }
                result.push_back( entry.utxo );
            }
            return result;
        }
        return {};
    }

    std::optional<GeniusUTXO> UTXOManager::GetUnconsumedUTXO( const base::Hash256 &txid, uint32_t output_idx ) const
    {
        std::shared_lock lock( utxos_mutex_ );
        const OutPoint   outpoint{ txid, output_idx };
        auto             it = utxo_outpoints_.find( outpoint );
        if ( it == utxo_outpoints_.end() || it->second.state == UTXOState::UTXO_CONSUMED )
        {
            return std::nullopt;
        }
        return it->second.utxo;
    }

    std::unordered_map<std::string, std::vector<UTXOManager::UTXOData>> UTXOManager::GetAllUTXOs() const
    {
        std::shared_lock                                       lock( utxos_mutex_ );
        std::unordered_map<std::string, std::vector<UTXOData>> result;
        for ( const auto &[outpoint, entry] : utxo_outpoints_ )
        {
            (void) outpoint;
            const auto &owner = entry.utxo.GetOwnerAddress();
            result[owner].emplace_back( entry.state, entry.utxo );
        }
        return result;
    }

    outcome::result<void> UTXOManager::SetUTXOs( const std::vector<GeniusUTXO> &utxos, const std::string &address )
    {
        // D-17: foreign-address guard removed — all nodes manage UTXOs for all peers

        {
            std::unique_lock lock( utxos_mutex_ );

            if ( auto address_it = address_outpoints_.find( address ); address_it != address_outpoints_.end() )
            {
                for ( const auto &outpoint : address_it->second )
                {
                    local_reservations_.erase( outpoint );
                    utxo_outpoints_.erase( outpoint );
                }
                address_it->second.clear();
            }

            auto &outpoints = address_outpoints_[address];
            outpoints.clear(); //TODO - Evaluate if this is necessary, since it already clears on the loop above.
            outpoints.reserve( utxos.size() );
            for ( const auto &utxo : utxos )
            {
                auto owned_utxo = utxo;
                owned_utxo.SetOwnerAddress( address );
                const OutPoint outpoint{ owned_utxo.GetTxID(), owned_utxo.GetOutputIdx() };
                utxo_outpoints_[outpoint] = UTXOEntry{ UTXOState::UTXO_READY,
                                                       owned_utxo,
                                                       0,
                                                       std::nullopt,
                                                       std::nullopt,
                                                       UTXOType::UTXO_NORMAL };
                outpoints.push_back( outpoint );
            }
        }

        if ( auto res = StoreUTXOs( address ); res.has_error() )
        {
            return res.error();
        }

        logger_->debug( "Set {} UTXOs for address {}", utxos.size(), address.substr( 0, 8 ) );
        return outcome::success();
    }

    outcome::result<UTXOTxParameters> UTXOManager::CreateTxParameter( uint64_t    amount,
                                                                      std::string dest_address,
                                                                      TokenID     token_id )
    {
        BOOST_OUTCOME_TRY( auto selection_result, SelectUTXOs( amount, token_id ) );
        auto [inputs, selected_amount] = selection_result;

        std::vector<OutputDestInfo> outputs;
        // Reserve space: one output per token plus possible change
        outputs.reserve( 2 );

        // Primary output
        outputs.push_back( { amount, std::move( dest_address ), token_id } );

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

        BOOST_OUTCOME_TRY( auto selection_result, SelectUTXOs( total_amount, token_id ) );
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

    void UTXOManager::ReserveUTXOs( const std::vector<InputUTXOInfo> &inputs,
                                    const std::string                &reservation_id,
                                    UTXOType                          type )
    {
        std::unique_lock lock( utxos_mutex_ );

        for ( const auto &input_utxo : inputs )
        {
            const OutPoint outpoint{ input_utxo.txid_hash_, input_utxo.output_idx_ };

            if ( auto entry_it = utxo_outpoints_.find( outpoint ); entry_it != utxo_outpoints_.end() )
            {
                if ( entry_it->second.state == UTXOState::UTXO_READY && entry_it->second.type == type )
                {
                    entry_it->second.state        = UTXOState::UTXO_RESERVED;
                    local_reservations_[outpoint] = reservation_id;
                }
                else if ( auto reservation_it = local_reservations_.find( outpoint );
                          entry_it->second.state == UTXOState::UTXO_RESERVED &&
                          reservation_it != local_reservations_.end() && reservation_it->second != reservation_id )
                {
                    logger_->warn( "Outpoint {}:{} already reserved by another tx",
                                   input_utxo.txid_hash_.toReadableString(),
                                   input_utxo.output_idx_ );
                }
            }
        }
    }

    void UTXOManager::RollbackUTXOs( const std::vector<InputUTXOInfo> &inputs,
                                     const std::string                &reservation_id,
                                     UTXOType                          type )
    {
        std::unique_lock lock( utxos_mutex_ );

        for ( const auto &input_utxo : inputs )
        {
            const OutPoint outpoint{ input_utxo.txid_hash_, input_utxo.output_idx_ };

            auto reservation_it = local_reservations_.find( outpoint );
            if ( auto entry_it = utxo_outpoints_.find( outpoint );
                 entry_it != utxo_outpoints_.end() && entry_it->second.state == UTXOState::UTXO_RESERVED &&
                 entry_it->second.type == type &&
                 ( reservation_id.empty() ||
                   ( reservation_it != local_reservations_.end() && reservation_it->second == reservation_id ) ) )
            {
                entry_it->second.state = UTXOState::UTXO_READY;
                if ( reservation_it != local_reservations_.end() )
                {
                    local_reservations_.erase( reservation_it );
                }
            }
        }
    }

    bool UTXOManager::VerifyParameters( const UTXOTxParameters &params, const std::string &address ) const
    {
        uint64_t expected_amount = 0;

        std::shared_lock lock( utxos_mutex_ );

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

            if ( utxo_it->second.state != UTXOState::UTXO_READY && utxo_it->second.state != UTXOState::UTXO_RESERVED )
            {
                logger_->warn( "Outpoint {}:{} is not spendable",
                               input.txid_hash_.toReadableString(),
                               input.output_idx_ );
                return false;
            }

            const auto &owner_address          = utxo_it->second.utxo.GetOwnerAddress();
            const bool  delegated_escrow_spend = owner_address != address && input.output_idx_ == 0 &&
                                                 utxo_address::IsEscrowLockAddress( owner_address );

            if ( owner_address != address && !delegated_escrow_spend )
            {
                logger_->warn( "Outpoint {}:{} does not belong to {}",
                               input.txid_hash_.toReadableString(),
                               input.output_idx_,
                               address );
                return false;
            }

            if ( delegated_escrow_spend )
            {
                logger_->debug( "Allowing delegated escrow spend for outpoint {}:{} by {} (lock owner: {})",
                                input.txid_hash_.toReadableString(),
                                input.output_idx_,
                                address.substr( 0, 8 ),
                                owner_address );
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

    std::optional<UTXOManager::UTXOState> UTXOManager::GetOutPointState( const base::Hash256 &utxo_id,
                                                                         uint32_t             output_idx ) const
    {
        std::shared_lock lock( utxos_mutex_ );
        const OutPoint   outpoint{ utxo_id, output_idx };
        auto             it = utxo_outpoints_.find( outpoint );
        if ( it == utxo_outpoints_.end() )
        {
            return std::nullopt;
        }
        return it->second.state;
    }

    bool UTXOManager::IsOutPointConsumed( const base::Hash256 &utxo_id, uint32_t output_idx ) const
    {
        auto state = GetOutPointState( utxo_id, output_idx );
        return state.has_value() && state.value() == UTXOState::UTXO_CONSUMED;
    }

    bool UTXOManager::IsOutPointReserved( const base::Hash256 &utxo_id, uint32_t output_idx ) const
    {
        auto state = GetOutPointState( utxo_id, output_idx );
        return state.has_value() && state.value() == UTXOState::UTXO_RESERVED;
    }

    base::Hash256 UTXOManager::ComputeUTXOMerkleRoot() const
    {
        return ComputeUTXOMerkleRoot( address_ );
    }

    base::Hash256 UTXOManager::ComputeUTXOMerkleRoot( const std::string &address ) const
    {
        // D-17: foreign-address guard removed — validators need Merkle roots for all addresses

        std::vector<GeniusUTXO> unspent;
        {
            std::shared_lock lock( utxos_mutex_ );
            auto             it = address_outpoints_.find( address );
            if ( it == address_outpoints_.end() )
            {
                return utxo_merkle::EmptyUTXOMerkleRoot();
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

        return ComputeMerkleRootFromUTXOList( std::move( unspent ) );
    }

    base::Hash256 UTXOManager::ComputeUTXOMerkleRootFromSnapshot( const std::vector<GeniusUTXO> &utxos ) const
    {
        return ComputeMerkleRootFromUTXOList( utxos );
    }

    outcome::result<bool> UTXOManager::LoadUTXOs( std::shared_ptr<storage::rocksdb> db )
    {
        if ( db == nullptr )
        {
            logger_->error( "Tried to initialize DB with null pointer" );
            return std::errc::invalid_argument;
        }

        std::unique_lock persistence_lock( persistence_mutex_ );
        {
            std::unique_lock lock( utxos_mutex_ );
            if ( db_ != nullptr )
            {
                logger_->warn( "UTXOs were already loaded" );
            }
            db_ = std::move( db );
            utxo_outpoints_.clear();
            address_outpoints_.clear();
            local_reservations_.clear();
        }

        std::shared_ptr<storage::rocksdb> db_handle;
        {
            std::shared_lock lock( utxos_mutex_ );
            db_handle = db_;
        }
        if ( db_handle == nullptr )
        {
            logger_->error( "Tried to query UTXOs without loading DB" );
            return storage::DatabaseError::UNITIALIZED;
        }

        base::Buffer key_buf;
        key_buf.put( DB_PREFIX );
        auto utxo_list = db_handle->query( key_buf );

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

        {
            std::unique_lock lock( utxos_mutex_ );
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

                BOOST_OUTCOME_TRY( auto hash,
                                   base::Hash256::fromSpan( gsl::span( reinterpret_cast<uint8_t *>( const_cast<char *>(
                                                                           entry_record.utxo().hash().data() ) ),
                                                                       entry_record.utxo().hash().size() ) ) );

                auto       token_id = TokenID::FromBytes( entry_record.utxo().token().data(),
                                                          entry_record.utxo().token().size() );
                GeniusUTXO loaded_utxo( hash,
                                        entry_record.utxo().output_idx(),
                                        entry_record.utxo().amount(),
                                        token_id,
                                        address );
                const auto outpoint = loaded_utxo.GetOutPoint();
                UTXOEntry  loaded_entry;
                loaded_entry.state         = state;
                loaded_entry.type          = FromProtoType( entry_record.type() );
                loaded_entry.utxo          = loaded_utxo;
                loaded_entry.created_epoch = entry_record.created_epoch();
                if ( entry_record.has_spent_epoch() )
                {
                    loaded_entry.spent_epoch = entry_record.spent_epoch();
                }
                if ( entry_record.has_spent_by_txid() )
                {
                    BOOST_OUTCOME_TRY(
                        auto spent_by_hash,
                        base::Hash256::fromSpan( gsl::span(
                            reinterpret_cast<uint8_t *>( const_cast<char *>( entry_record.spent_by_txid().data() ) ),
                            entry_record.spent_by_txid().size() ) ) );
                    loaded_entry.spent_by_txid = spent_by_hash;
                }

                utxo_outpoints_[outpoint] = std::move( loaded_entry );
                address_outpoints_[address].push_back( outpoint );
            }
        }

        return !utxo_outpoints_.empty();
    }

    std::shared_ptr<storage::rocksdb> UTXOManager::AcquireStorage() const
    {
        std::lock_guard persistence_lock( persistence_mutex_ );
        std::shared_lock lock( utxos_mutex_ );
        return db_;
    }

    void UTXOManager::ReleaseStorage()
    {
        std::lock_guard persistence_lock( persistence_mutex_ );
        std::unique_lock lock( utxos_mutex_ );
        db_.reset();
    }

    outcome::result<void> UTXOManager::StoreUTXOs( const std::string &address )
    {
        BOOST_OUTCOME_TRY( InvokeFault( FaultStage::OrdinaryStoreWaitingForPersistenceGate ) );
        std::unique_lock persistence_lock( persistence_mutex_ );
        BOOST_OUTCOME_TRY( InvokeFault( FaultStage::OrdinaryStorePersistenceGateAcquired ) );
        std::shared_ptr<storage::rocksdb> db;
        {
            std::shared_lock lock( utxos_mutex_ );
            db = db_;
        }
        if ( db == nullptr )
        {
            logger_->error( "Tried to store UTXOs without loading DB" );
            return storage::DatabaseError::UNITIALIZED;
        }

        base::Buffer existing_prefix;
        existing_prefix.put( fmt::format( "{}/{}/", DB_PREFIX, address ) );

        auto existing_records = db->query( existing_prefix );
        if ( existing_records.has_error() && existing_records.error() != storage::DatabaseError::NOT_FOUND )
        {
            logger_->error( "Failed to query existing UTXO records for address {}", address );
            return existing_records.error();
        }

        auto batch = db->batch();
        if ( existing_records.has_value() )
        {
            for ( const auto &[existing_key, _] : existing_records.value() )
            {
                if ( auto rem_res = batch->remove( existing_key ); rem_res.has_error() )
                {
                    logger_->error( "Failed to remove old UTXO record for address {}", address );
                    return rem_res.error();
                }
            }
        }

        std::vector<std::pair<OutPoint, UTXOEntry>> entries_to_store;
        {
            std::shared_lock lock( utxos_mutex_ );
            entries_to_store.reserve( utxo_outpoints_.size() );
            for ( const auto &[outpoint, entry] : utxo_outpoints_ )
            {
                if ( entry.utxo.GetOwnerAddress() != address )
                {
                    continue;
                }
                entries_to_store.emplace_back( outpoint, entry );
            }
        }

        uint64_t stored = 0;
        for ( const auto &[outpoint, entry] : entries_to_store )
        {
            BOOST_OUTCOME_TRY( auto value_buf, SerializeUTXOEntry( entry, address ) );

            base::Buffer key_buf;
            key_buf.put( BuildUTXORecordKey( address, outpoint ) );

            if ( auto put_res = batch->put( key_buf, value_buf ); put_res.has_error() )
            {
                logger_->error( "Error when storing UTXO record for address {}", address );
                return put_res.error();
            }
            ++stored;
        }

        BOOST_OUTCOME_TRY( InvokeFault( FaultStage::OrdinaryStoreSnapshotReadyBeforeCommit ) );
        if ( auto commit_res = batch->commit(); commit_res.has_error() )
        {
            logger_->error( "Error when committing UTXO records for address {}", address );
            return commit_res.error();
        }

        logger_->info( "Stored {} UTXOs for address {}", stored, address );
        return outcome::success();
    }

    std::string UTXOManager::MakeBridgeApplicationKey( const std::string &chain_id,
                                                        const base::Hash256 &burn_hash,
                                                        uint32_t receipt_log_index )
    {
        return fmt::format( "{}{}:{}:{}",
                            BRIDGE_APPLICATION_PREFIX,
                            chain_id,
                            burn_hash.toReadableString(),
                            receipt_log_index );
    }

    outcome::result<void> UTXOManager::InvokeFault( FaultStage stage ) const
    {
        return fault_callback_ ? fault_callback_( stage ) : outcome::success();
    }

    void UTXOManager::ResetFaultCallback()
    {
        fault_callback_ = []( FaultStage ) -> outcome::result<void>
        {
            return outcome::success();
        };
    }

    void UTXOManager::ResetBridgeApplicationReader()
    {
        bridge_application_reader_ =
            []( const std::shared_ptr<storage::rocksdb> &db,
                const base::Buffer &key ) -> outcome::result<base::Buffer>
        {
            if ( !db )
            {
                return outcome::failure( storage::DatabaseError::UNITIALIZED );
            }
            return db->get( key );
        };
    }

    outcome::result<UTXOManager::AtomicMintEffectResult>
    UTXOManager::ApplyMintEffectsAtomically( const AtomicMintEffectRequest &request )
    {
        if ( request.chain_id.empty() || request.burn_hash == base::Hash256{} ||
             request.winning_transaction_hash == base::Hash256{} ||
             request.bridge_input.txid_hash_ != request.burn_hash ||
             request.bridge_input.output_idx_ != request.receipt_log_index ||
             request.bridge_input_owner.empty() ||
             request.bridge_input_type != UTXOType::UTXO_BRIDGE )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        SGTransaction::BridgeApplicationRecord application;
        application.set_version( 1 );
        application.set_chain_id( request.chain_id );
        application.set_burn_hash( request.burn_hash.data(), request.burn_hash.size() );
        application.set_receipt_log_index( request.receipt_log_index );
        application.set_winning_transaction_hash( request.winning_transaction_hash.data(),
                                                  request.winning_transaction_hash.size() );
        application.set_bridge_input_owner( request.bridge_input_owner );
        application.set_bridge_input_type( ToProtoType( request.bridge_input_type ) );
        for ( const auto &output : request.produced_outputs )
        {
            auto *record = application.add_produced_outputs();
            auto *utxo = record->mutable_utxo();
            const auto txid = output.GetTxID();
            const auto token = output.GetTokenID();
            utxo->set_hash( txid.data(), txid.size() );
            utxo->set_token( token.bytes().data(), token.size() );
            utxo->set_amount( output.GetAmount() );
            utxo->set_output_idx( output.GetOutputIdx() );
            record->set_owner_address( output.GetOwnerAddress() );
            record->set_state( SGTransaction::UTXO_ENTRY_READY );
            record->set_type( SGTransaction::UTXO_ENTRY_NORMAL );
        }
        std::string application_bytes;
        if ( !application.SerializeToString( &application_bytes ) )
        {
            return outcome::failure( std::errc::bad_message );
        }

        BOOST_OUTCOME_TRY( InvokeFault( FaultStage::AtomicMintWaitingForPersistenceGate ) );
        std::unique_lock persistence_lock( persistence_mutex_ );
        BOOST_OUTCOME_TRY( InvokeFault( FaultStage::AtomicMintPersistenceGateAcquired ) );
        std::unique_lock state_lock( utxos_mutex_ );
        auto db = db_;
        if ( !db )
        {
            return outcome::failure( storage::DatabaseError::UNITIALIZED );
        }

        base::Buffer application_key;
        application_key.put( MakeBridgeApplicationKey(
            request.chain_id, request.burn_hash, request.receipt_log_index ) );
        auto existing_application = bridge_application_reader_( db, application_key );
        if ( existing_application.has_value() )
        {
            if ( existing_application.value().toString() != application_bytes )
            {
                return outcome::failure( std::errc::state_not_recoverable );
            }
            for ( const auto &output : request.produced_outputs )
            {
                auto it = utxo_outpoints_.find( output.GetOutPoint() );
                if ( it == utxo_outpoints_.end() ||
                     it->second.state != UTXOState::UTXO_READY ||
                     it->second.type != UTXOType::UTXO_NORMAL ||
                     it->second.utxo.GetOwnerAddress() != output.GetOwnerAddress() ||
                     it->second.utxo.GetAmount() != output.GetAmount() ||
                     !it->second.utxo.GetTokenID().Equals( output.GetTokenID() ) )
                {
                    return outcome::failure( std::errc::state_not_recoverable );
                }
            }
            const OutPoint bridge_outpoint{ request.burn_hash, request.receipt_log_index };
            auto bridge_it = utxo_outpoints_.find( bridge_outpoint );
            if ( bridge_it == utxo_outpoints_.end() ||
                 bridge_it->second.state != UTXOState::UTXO_CONSUMED ||
                 bridge_it->second.type != UTXOType::UTXO_BRIDGE ||
                 bridge_it->second.utxo.GetOwnerAddress() != request.bridge_input_owner )
            {
                return outcome::failure( std::errc::state_not_recoverable );
            }
            return AtomicMintEffectResult::AlreadyApplied;
        }
        if ( existing_application.error() != storage::DatabaseError::NOT_FOUND )
        {
            return outcome::failure( existing_application.error() );
        }

        auto candidate_outpoints = utxo_outpoints_;
        auto candidate_addresses = address_outpoints_;
        std::unordered_set<std::string> affected_owners;
        for ( const auto &output : request.produced_outputs )
        {
            if ( output.GetTxID() != request.winning_transaction_hash ||
                 output.GetOwnerAddress().empty() )
            {
                return outcome::failure( std::errc::invalid_argument );
            }
            const auto outpoint = output.GetOutPoint();
            auto existing = candidate_outpoints.find( outpoint );
            if ( existing != candidate_outpoints.end() )
            {
                const auto &entry = existing->second;
                if ( entry.state != UTXOState::UTXO_READY ||
                     entry.type != UTXOType::UTXO_NORMAL ||
                     entry.utxo.GetOwnerAddress() != output.GetOwnerAddress() ||
                     entry.utxo.GetAmount() != output.GetAmount() ||
                     !entry.utxo.GetTokenID().Equals( output.GetTokenID() ) )
                {
                    return outcome::failure( std::errc::file_exists );
                }
            }
            else
            {
                UTXOEntry entry;
                entry.utxo = output;
                candidate_outpoints.emplace( outpoint, entry );
                candidate_addresses[output.GetOwnerAddress()].push_back( outpoint );
            }
            affected_owners.insert( output.GetOwnerAddress() );
            BOOST_OUTCOME_TRY( InvokeFault( FaultStage::ProducedOutputStage ) );
        }

        BOOST_OUTCOME_TRY( InvokeFault( FaultStage::BridgeInputStage ) );
        const OutPoint bridge_outpoint{ request.burn_hash, request.receipt_log_index };
        auto bridge_it = candidate_outpoints.find( bridge_outpoint );
        if ( bridge_it == candidate_outpoints.end() ||
             bridge_it->second.type != UTXOType::UTXO_BRIDGE ||
             bridge_it->second.utxo.GetOwnerAddress() != request.bridge_input_owner ||
             ( bridge_it->second.state != UTXOState::UTXO_READY &&
               bridge_it->second.state != UTXOState::UTXO_RESERVED ) )
        {
            return outcome::failure( std::errc::state_not_recoverable );
        }
        bridge_it->second.state = UTXOState::UTXO_CONSUMED;
        auto &bridge_owner_points = candidate_addresses[request.bridge_input_owner];
        bridge_owner_points.erase(
            std::remove( bridge_owner_points.begin(), bridge_owner_points.end(), bridge_outpoint ),
            bridge_owner_points.end() );
        affected_owners.insert( request.bridge_input_owner );

        auto batch = db->batch();
        for ( const auto &owner : affected_owners )
        {
            base::Buffer prefix;
            prefix.put( fmt::format( "{}/{}/", DB_PREFIX, owner ) );
            auto existing = db->query( prefix );
            if ( existing.has_error() && existing.error() != storage::DatabaseError::NOT_FOUND )
            {
                return outcome::failure( existing.error() );
            }
            if ( existing.has_value() )
            {
                for ( const auto &[key, ignored] : existing.value() )
                {
                    (void) ignored;
                    BOOST_OUTCOME_TRY( batch->remove( key ) );
                }
            }
            for ( const auto &[outpoint, entry] : candidate_outpoints )
            {
                if ( entry.utxo.GetOwnerAddress() != owner )
                {
                    continue;
                }
                BOOST_OUTCOME_TRY( auto value, SerializeUTXOEntry( entry, owner ) );
                base::Buffer key;
                key.put( BuildUTXORecordKey( owner, outpoint ) );
                BOOST_OUTCOME_TRY( batch->put( key, value ) );
            }
        }
        base::Buffer application_value;
        application_value.put( application_bytes );
        BOOST_OUTCOME_TRY( batch->put( application_key, application_value ) );
        BOOST_OUTCOME_TRY( InvokeFault( FaultStage::AtomicMintBeforeBatchCommit ) );
        BOOST_OUTCOME_TRY( batch->commit() );

        utxo_outpoints_.swap( candidate_outpoints );
        address_outpoints_.swap( candidate_addresses );
        local_reservations_.erase( bridge_outpoint );
        return AtomicMintEffectResult::Applied;
    }

    outcome::result<std::optional<UTXOManager::BridgeApplication>>
    UTXOManager::GetBridgeApplication( const std::string &chain_id,
                                        const base::Hash256 &burn_hash,
                                        uint32_t receipt_log_index ) const
    {
        std::unique_lock persistence_lock( persistence_mutex_ );
        std::shared_lock state_lock( utxos_mutex_ );
        auto db = db_;
        if ( !db )
        {
            return outcome::failure( storage::DatabaseError::UNITIALIZED );
        }
        base::Buffer key;
        key.put( MakeBridgeApplicationKey( chain_id, burn_hash, receipt_log_index ) );
        auto raw = bridge_application_reader_( db, key );
        if ( raw.has_error() )
        {
            if ( raw.error() == storage::DatabaseError::NOT_FOUND )
            {
                return std::optional<BridgeApplication>{};
            }
            return outcome::failure( raw.error() );
        }

        SGTransaction::BridgeApplicationRecord record;
        if ( !record.ParseFromArray( raw.value().data(), raw.value().size() ) ||
             record.version() != 1 || record.chain_id() != chain_id ||
             record.receipt_log_index() != receipt_log_index ||
             record.bridge_input_type() != SGTransaction::UTXO_ENTRY_BRIDGE )
        {
            return outcome::failure( std::errc::bad_message );
        }
        BOOST_OUTCOME_TRY(
            auto record_burn,
            base::Hash256::fromSpan( gsl::span(
                reinterpret_cast<uint8_t *>( const_cast<char *>( record.burn_hash().data() ) ),
                record.burn_hash().size() ) ) );
        BOOST_OUTCOME_TRY(
            auto winner,
            base::Hash256::fromSpan( gsl::span(
                reinterpret_cast<uint8_t *>( const_cast<char *>( record.winning_transaction_hash().data() ) ),
                record.winning_transaction_hash().size() ) ) );
        if ( record_burn != burn_hash || winner == base::Hash256{} ||
             record.bridge_input_owner().empty() )
        {
            return outcome::failure( std::errc::bad_message );
        }

        BridgeApplication result;
        result.winning_transaction_hash = winner;
        result.chain_id = chain_id;
        result.burn_hash = burn_hash;
        result.receipt_log_index = receipt_log_index;
        result.bridge_input_owner = record.bridge_input_owner();
        result.bridge_input_type = UTXOType::UTXO_BRIDGE;
        result.canonical_bytes.assign( raw.value().begin(), raw.value().end() );
        for ( const auto &produced : record.produced_outputs() )
        {
            if ( produced.state() != SGTransaction::UTXO_ENTRY_READY ||
                 produced.type() != SGTransaction::UTXO_ENTRY_NORMAL ||
                 produced.owner_address().empty() )
            {
                return outcome::failure( std::errc::bad_message );
            }
            BOOST_OUTCOME_TRY(
                auto txid,
                base::Hash256::fromSpan( gsl::span(
                    reinterpret_cast<uint8_t *>( const_cast<char *>( produced.utxo().hash().data() ) ),
                    produced.utxo().hash().size() ) ) );
            if ( txid != winner )
            {
                return outcome::failure( std::errc::bad_message );
            }
            auto token = TokenID::FromBytes( produced.utxo().token().data(),
                                             produced.utxo().token().size() );
            GeniusUTXO output( txid,
                               produced.utxo().output_idx(),
                               produced.utxo().amount(),
                               token,
                               produced.owner_address() );
            auto live = utxo_outpoints_.find( output.GetOutPoint() );
            if ( live == utxo_outpoints_.end() ||
                 live->second.state != UTXOState::UTXO_READY ||
                 live->second.type != UTXOType::UTXO_NORMAL ||
                 live->second.utxo.GetOwnerAddress() != output.GetOwnerAddress() ||
                 live->second.utxo.GetAmount() != output.GetAmount() ||
                 !live->second.utxo.GetTokenID().Equals( output.GetTokenID() ) )
            {
                return outcome::failure( std::errc::state_not_recoverable );
            }
            result.produced_outputs.push_back( std::move( output ) );
        }
        const OutPoint bridge_outpoint{ burn_hash, receipt_log_index };
        auto bridge = utxo_outpoints_.find( bridge_outpoint );
        if ( bridge == utxo_outpoints_.end() ||
             bridge->second.state != UTXOState::UTXO_CONSUMED ||
             bridge->second.type != UTXOType::UTXO_BRIDGE ||
             bridge->second.utxo.GetOwnerAddress() != record.bridge_input_owner() )
        {
            return outcome::failure( std::errc::state_not_recoverable );
        }
        return std::optional<BridgeApplication>{ std::move( result ) };
    }

    outcome::result<void> UTXOManager::CreateCheckpoint( uint64_t             epoch,
                                                         const base::Hash256 &last_finalized_tx,
                                                         const base::Hash256 &registry_hash )
    {
        return CreateCheckpoint( address_, epoch, last_finalized_tx, registry_hash );
    }

    outcome::result<void> UTXOManager::CreateCheckpoint( const std::string   &address,
                                                         uint64_t             epoch,
                                                         const base::Hash256 &last_finalized_tx,
                                                         const base::Hash256 &registry_hash )
    {
        std::unique_lock persistence_lock( persistence_mutex_ );
        std::shared_ptr<storage::rocksdb> db;
        {
            std::shared_lock lock( utxos_mutex_ );
            db = db_;
        }
        if ( db == nullptr )
        {
            logger_->error( "Tried to create checkpoint without loading DB" );
            return storage::DatabaseError::UNITIALIZED;
        }

        // D-17: foreign-address guard removed — all nodes manage checkpoints for all peers

        std::vector<GeniusUTXO> unspent_snapshot;
        {
            std::shared_lock lock( utxos_mutex_ );
            if ( auto address_it = address_outpoints_.find( address ); address_it != address_outpoints_.end() )
            {
                unspent_snapshot.reserve( address_it->second.size() );
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
                    unspent_snapshot.push_back( utxo_it->second.utxo );
                }
            }
        }

        SGTransaction::UTXOCheckpointRecord checkpoint_record;
        checkpoint_record.set_owner_address( address );
        checkpoint_record.set_epoch( epoch );
        checkpoint_record.set_last_finalized_tx( last_finalized_tx.data(), last_finalized_tx.size() );
        checkpoint_record.set_registry_hash( registry_hash.data(), registry_hash.size() );
        const auto utxo_root = ComputeMerkleRootFromUTXOList( unspent_snapshot );
        checkpoint_record.set_utxo_merkle_root( utxo_root.data(), utxo_root.size() );
        checkpoint_record.set_utxo_count( unspent_snapshot.size() );
        const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch() );
        checkpoint_record.set_created_at_ms( static_cast<uint64_t>( now_ms.count() ) );

        base::Buffer checkpoint_value_buf( std::vector<uint8_t>( checkpoint_record.ByteSizeLong() ) );
        if ( !checkpoint_record.SerializeToArray( checkpoint_value_buf.data(), checkpoint_value_buf.size() ) )
        {
            logger_->error( "Failed to serialize checkpoint for address {}", address );
            return std::errc::bad_message;
        }

        const auto   checkpoint_key = BuildCheckpointRecordKey( address, epoch );
        base::Buffer checkpoint_key_buf;
        checkpoint_key_buf.put( checkpoint_key );
        if ( auto put_res = db->put( checkpoint_key_buf, checkpoint_value_buf ); put_res.has_error() )
        {
            logger_->error( "Failed to store checkpoint record for address {}", address );
            return put_res.error();
        }

        base::Buffer latest_pointer_key_buf;
        latest_pointer_key_buf.put( BuildLatestCheckpointPointerKey( address ) );
        base::Buffer latest_pointer_value_buf;
        latest_pointer_value_buf.put( checkpoint_key );
        if ( auto put_latest_res = db->put( latest_pointer_key_buf, latest_pointer_value_buf );
             put_latest_res.has_error() )
        {
            logger_->error( "Failed to store checkpoint latest pointer for address {}", address );
            return put_latest_res.error();
        }

        logger_->info( "Created checkpoint owner={} epoch={} utxo_count={}", address, epoch, unspent_snapshot.size() );
        return outcome::success();
    }

    outcome::result<std::optional<UTXOManager::UTXOCheckpoint>> UTXOManager::LoadLatestCheckpoint(
        const std::string &address ) const
    {
        std::unique_lock persistence_lock( persistence_mutex_ );
        std::shared_ptr<storage::rocksdb> db;
        {
            std::shared_lock lock( utxos_mutex_ );
            db = db_;
        }
        if ( db == nullptr )
        {
            logger_->error( "Tried to load checkpoint without loading DB" );
            return storage::DatabaseError::UNITIALIZED;
        }

        // D-17: foreign-address guard removed — all nodes manage checkpoints for all peers

        base::Buffer latest_pointer_key_buf;
        latest_pointer_key_buf.put( BuildLatestCheckpointPointerKey( address ) );
        auto latest_pointer_value = db->get( latest_pointer_key_buf );
        if ( latest_pointer_value.has_error() )
        {
            if ( latest_pointer_value.error() == storage::DatabaseError::NOT_FOUND )
            {
                return std::optional<UTXOCheckpoint>{};
            }
            logger_->error( "Failed to load latest checkpoint pointer for address {}", address );
            return latest_pointer_value.error();
        }

        base::Buffer checkpoint_key_buf;
        checkpoint_key_buf.put( latest_pointer_value.value().toString() );
        auto checkpoint_value = db->get( checkpoint_key_buf );
        if ( checkpoint_value.has_error() )
        {
            if ( checkpoint_value.error() == storage::DatabaseError::NOT_FOUND )
            {
                return std::optional<UTXOCheckpoint>{};
            }
            logger_->error( "Failed to load checkpoint record for address {}", address );
            return checkpoint_value.error();
        }

        SGTransaction::UTXOCheckpointRecord checkpoint_record;
        if ( !checkpoint_record.ParseFromArray( checkpoint_value.value().data(), checkpoint_value.value().size() ) )
        {
            logger_->error( "Failed to deserialize checkpoint record for address {}", address );
            return std::errc::bad_message;
        }

        BOOST_OUTCOME_TRY( auto last_finalized_tx_hash,
                           base::Hash256::fromSpan( gsl::span( reinterpret_cast<uint8_t *>( const_cast<char *>(
                                                                   checkpoint_record.last_finalized_tx().data() ) ),
                                                               checkpoint_record.last_finalized_tx().size() ) ) );
        BOOST_OUTCOME_TRY( auto registry_hash,
                           base::Hash256::fromSpan( gsl::span( reinterpret_cast<uint8_t *>( const_cast<char *>(
                                                                   checkpoint_record.registry_hash().data() ) ),
                                                               checkpoint_record.registry_hash().size() ) ) );
        BOOST_OUTCOME_TRY( auto utxo_root_hash,
                           base::Hash256::fromSpan( gsl::span( reinterpret_cast<uint8_t *>( const_cast<char *>(
                                                                   checkpoint_record.utxo_merkle_root().data() ) ),
                                                               checkpoint_record.utxo_merkle_root().size() ) ) );

        UTXOCheckpoint checkpoint;
        checkpoint.owner_address     = checkpoint_record.owner_address();
        checkpoint.epoch             = checkpoint_record.epoch();
        checkpoint.last_finalized_tx = last_finalized_tx_hash;
        checkpoint.registry_hash     = registry_hash;
        checkpoint.utxo_merkle_root  = utxo_root_hash;
        checkpoint.utxo_count        = checkpoint_record.utxo_count();
        checkpoint.created_at_ms     = checkpoint_record.created_at_ms();

        return std::optional<UTXOCheckpoint>{ checkpoint };
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
