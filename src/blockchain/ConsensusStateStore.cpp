#include "blockchain/ConsensusStateStore.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <utility>

#include "base/hexutil.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"
#include "crypto/hasher.hpp"
#include "libp2p/crypto/random_generator/boost_generator.hpp"
#include "storage/database_error.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns, ConsensusStateStoreError, error )
{
    using E = sgns::ConsensusStateStoreError;
    switch ( error )
    {
        case E::InvalidArgument:
            return "invalid consensus local-state argument";
        case E::Integrity:
            return "consensus local-state integrity error";
        case E::Conflict:
            return "consensus local-state conflict";
        case E::DatastoreIdentity:
            return "consensus local-state datastore identity mismatch";
        case E::Storage:
            return "consensus local-state storage error";
    }
    return "unknown consensus local-state error";
}

namespace sgns
{
    namespace
    {
        constexpr uint32_t    kSchemaVersion = 2;
        constexpr std::string_view kPrefix = "/consensus/local/v2/";
        constexpr std::string_view kVotePrefix = "/consensus/local/v2/vote/";
        constexpr std::string_view kProcessPrefix = "/consensus/local/v2/process/";
        constexpr std::string_view kConflictPrefix = "/consensus/local/v2/conflict/";
        constexpr std::string_view kSafetyPrefix = "/consensus/local/v2/safety/";
        constexpr std::string_view kBurnSlotPrefix = "/consensus/local/v2/burn/slot/";
        constexpr std::string_view kBurnOutpointPrefix = "/consensus/local/v2/burn/outpoint/";

        base::Buffer BufferOf( std::string_view value )
        {
            base::Buffer buffer;
            buffer.put( value );
            return buffer;
        }

        std::string ValidatorHash( const std::string &validator_id )
        {
            auto hash = crypto::sha2_256( validator_id.data(), validator_id.size() );
            return base::hex_lower( gsl::span<const uint8_t>( hash.data(), hash.size() ) );
        }

        bool IsCanonicalHash( std::string_view value )
        {
            return value.size() == 64 &&
                   std::all_of( value.begin(), value.end(), []( unsigned char c )
                                { return std::isdigit( c ) || ( c >= 'a' && c <= 'f' ); } );
        }

        bool IsNonzeroCanonicalHash( std::string_view value )
        {
            return IsCanonicalHash( value ) && value.find_first_not_of( '0' ) != std::string_view::npos;
        }

        bool IsCanonicalChain( std::string_view value )
        {
            return !value.empty() && ( value == "0" || value.front() != '0' ) &&
                   std::all_of( value.begin(), value.end(), []( unsigned char c ) { return std::isdigit( c ) != 0; } );
        }

        std::string HashText( std::string_view value )
        {
            const auto hash = crypto::sha2_256( value.data(), value.size() );
            return base::hex_lower( gsl::span<const uint8_t>( hash.data(), hash.size() ) );
        }

        std::string BurnSlotFor( const ConsensusStateStore::BurnOutpoint &outpoint )
        {
            return HashText( std::string( "mint-v2:" ) + outpoint.source_chain + ":" + outpoint.burn_hash + ":" +
                             std::to_string( outpoint.receipt_log_index ) );
        }

        bool SameOutpoint( const ConsensusStateStore::BurnReservationRecord &record,
                           const ConsensusStateStore::BurnOutpoint &outpoint )
        {
            return record.source_chain() == outpoint.source_chain && record.burn_hash() == outpoint.burn_hash &&
                   record.receipt_log_index() == outpoint.receipt_log_index;
        }

        bool SameFinality( const ConsensusStateStore::BurnReservationRecord &record,
                           const std::string &certificate_digest,
                           const std::string &proposal_id,
                           const std::string &winner_id )
        {
            return record.certificate_digest() == certificate_digest && record.proposal_id() == proposal_id &&
                   record.winner_id() == winner_id;
        }

        outcome::result<std::string> RandomGeneration()
        {
            try
            {
                libp2p::crypto::random::BoostRandomGenerator random;
                const auto bytes = random.randomBytes( 32 );
                return base::hex_lower( gsl::span<const uint8_t>( bytes.data(), bytes.size() ) );
            }
            catch ( ... )
            {
                return outcome::failure( ConsensusStateStoreError::Storage );
            }
        }

        template <typename Message>
        bool ParseStrict( std::string_view bytes, Message &message )
        {
            if ( bytes.empty() || !message.ParseFromArray( bytes.data(), static_cast<int>( bytes.size() ) ) ||
                 !message.GetReflection()->GetUnknownFields( message ).empty() )
            {
                return false;
            }
            std::string canonical;
            return message.SerializeToString( &canonical ) && canonical == bytes;
        }

        template <typename Message>
        outcome::result<base::Buffer> SerializeStrict( const Message &message )
        {
            std::string bytes;
            if ( !message.SerializeToString( &bytes ) )
            {
                return outcome::failure( ConsensusStateStoreError::Integrity );
            }
            return BufferOf( bytes );
        }

        bool SameVoteIdentityAndBytes( const ConsensusStateStore::VoteRecord &lhs,
                                       const ConsensusStateStore::VoteRecord &rhs )
        {
            return lhs.slot_id() == rhs.slot_id() && lhs.proposal_id() == rhs.proposal_id() &&
                   lhs.validator_id() == rhs.validator_id() && lhs.generation() == rhs.generation() &&
                   lhs.signed_vote_bytes() == rhs.signed_vote_bytes() &&
                   lhs.outbound_envelope_bytes() == rhs.outbound_envelope_bytes() &&
                   lhs.signed_proposal_bytes() == rhs.signed_proposal_bytes() &&
                   lhs.registry_cid() == rhs.registry_cid() && lhs.registry_epoch() == rhs.registry_epoch() &&
                   lhs.created_at_ms() == rhs.created_at_ms() &&
                   lhs.acceptance_horizon_ms() == rhs.acceptance_horizon_ms();
        }

        template <typename Record, typename Validator>
        outcome::result<std::vector<Record>> StrictScan( const storage::rocksdb::QueryResult &raw,
                                                         Validator                           validate )
        {
            std::vector<Record> records;
            records.reserve( raw.size() );
            for ( const auto &[key, value] : raw )
            {
                Record record;
                if ( !ParseStrict( value.toString(), record ) )
                {
                    return outcome::failure( ConsensusStateStoreError::Integrity );
                }
                BOOST_OUTCOME_TRY( validate( record, std::string( key.toString() ) ) );
                records.push_back( std::move( record ) );
            }
            return records;
        }
    } // namespace

    ConsensusStateStore::ConsensusStateStore( std::shared_ptr<storage::rocksdb> datastore )
        : datastore_( std::move( datastore ) )
    {
        query_ = [this]( const base::Buffer &prefix ) { return datastore_->query( prefix ); };
        commit_ = []( storage::BufferBatch &batch ) { return batch.commit(); };
    }

    std::string ConsensusStateStore::VoteKey( const std::string &validator_id, const std::string &slot_id )
    {
        return std::string( kVotePrefix ) + ValidatorHash( validator_id ) + "/" + slot_id;
    }

    std::string ConsensusStateStore::ProcessKey( const std::string &slot_id )
    {
        return std::string( kProcessPrefix ) + slot_id;
    }

    std::string ConsensusStateStore::ConflictKey( const std::string &slot_id,
                                                   const std::string &low_digest,
                                                   const std::string &high_digest )
    {
        return std::string( kConflictPrefix ) + slot_id + "/" + low_digest + ":" + high_digest;
    }

    std::string ConsensusStateStore::SafetyKey( const std::string &slot_id )
    {
        return std::string( kSafetyPrefix ) + slot_id;
    }

    std::string ConsensusStateStore::BurnSlotKey( const std::string &slot_id )
    {
        return std::string( kBurnSlotPrefix ) + slot_id;
    }

    std::string ConsensusStateStore::BurnOutpointKey( const BurnOutpoint &outpoint )
    {
        return std::string( kBurnOutpointPrefix ) + BurnSlotFor( outpoint );
    }

    outcome::result<void> ConsensusStateStore::ValidateVote( const VoteRecord &record, const std::string &key ) const
    {
        if ( record.schema_version() != kSchemaVersion ||
             ( record.state() != VoteRecord::ACTIVE && record.state() != VoteRecord::RETIRED ) ||
             !IsCanonicalHash( record.slot_id() ) || !IsCanonicalHash( record.proposal_id() ) ||
             record.validator_id().empty() || key != VoteKey( record.validator_id(), record.slot_id() ) ||
             record.generation() == 0 || record.created_at_ms() == 0 ||
             record.acceptance_horizon_ms() < record.created_at_ms() || record.signed_vote_bytes().empty() ||
             record.outbound_envelope_bytes().empty() || record.signed_proposal_bytes().empty() ||
             record.registry_cid().empty() )
        {
            return outcome::failure( ConsensusStateStoreError::Integrity );
        }
        ConsensusVote vote;
        ConsensusMessage envelope;
        if ( !ParseStrict( record.signed_vote_bytes(), vote ) || !ParseStrict( record.outbound_envelope_bytes(), envelope ) ||
             envelope.payload_case() != ConsensusMessage::kVote ||
             envelope.vote().SerializeAsString() != record.signed_vote_bytes() ||
             vote.proposal_id() != record.proposal_id() || vote.voter_id() != record.validator_id() ||
             vote.signature().empty() )
        {
            return outcome::failure( ConsensusStateStoreError::Integrity );
        }
        ConsensusProposal proposal;
        if ( !ParseStrict( record.signed_proposal_bytes(), proposal ) ||
             proposal.proposal_id() != record.proposal_id() || proposal.registry_cid() != record.registry_cid() ||
             proposal.registry_epoch() != record.registry_epoch() || proposal.signature().empty() )
        {
            return outcome::failure( ConsensusStateStoreError::Integrity );
        }
        return outcome::success();
    }

    outcome::result<void> ConsensusStateStore::ValidateProcess( const ProcessRecord &record,
                                                                 const std::string   &key ) const
    {
        if ( record.schema_version() != kSchemaVersion ||
             ( record.state() != ProcessRecord::PENDING && record.state() != ProcessRecord::PROCESSING &&
               record.state() != ProcessRecord::COMPLETE ) ||
             !IsCanonicalHash( record.slot_id() ) || !IsCanonicalHash( record.certificate_digest() ) ||
             !IsCanonicalHash( record.proposal_id() ) || record.winner_id().empty() ||
             key != ProcessKey( record.slot_id() ) || record.updated_at_ms() == 0 ||
             ( record.state() == ProcessRecord::PROCESSING && record.lease_until_ms() <= record.updated_at_ms() ) ||
             ( record.state() != ProcessRecord::PROCESSING && record.lease_until_ms() != 0 ) )
        {
            return outcome::failure( ConsensusStateStoreError::Integrity );
        }
        return outcome::success();
    }

    outcome::result<void> ConsensusStateStore::ValidateConflict( const ConflictRecord &record,
                                                                  const std::string    &key ) const
    {
        if ( record.schema_version() != kSchemaVersion || !IsCanonicalHash( record.slot_id() ) ||
             !IsCanonicalHash( record.low_certificate_digest() ) ||
             !IsCanonicalHash( record.high_certificate_digest() ) ||
             record.low_certificate_digest() >= record.high_certificate_digest() ||
             !IsCanonicalHash( record.low_proposal_id() ) || !IsCanonicalHash( record.high_proposal_id() ) ||
             record.sources_bitset() == 0 || record.first_source() == 0 ||
             ( record.first_source() & ( record.first_source() - 1 ) ) != 0 ||
             ( record.sources_bitset() & record.first_source() ) == 0 || record.first_seen_at_ms() == 0 ||
             record.last_seen_at_ms() < record.first_seen_at_ms() || record.observation_count() == 0 ||
             !IsCanonicalHash( record.authoritative_certificate_digest() ) ||
             !IsCanonicalHash( record.authoritative_proposal_id() ) ||
             !IsCanonicalHash( record.incoming_certificate_digest() ) ||
             !IsCanonicalHash( record.incoming_proposal_id() ) ||
             record.authoritative_certificate_digest() == record.incoming_certificate_digest() ||
             key != ConflictKey( record.slot_id(),
                                 record.low_certificate_digest(),
                                 record.high_certificate_digest() ) )
        {
            return outcome::failure( ConsensusStateStoreError::Integrity );
        }
        const bool authoritative_is_low =
            record.authoritative_certificate_digest() == record.low_certificate_digest() &&
            record.authoritative_proposal_id() == record.low_proposal_id() &&
            record.incoming_certificate_digest() == record.high_certificate_digest() &&
            record.incoming_proposal_id() == record.high_proposal_id();
        const bool authoritative_is_high =
            record.authoritative_certificate_digest() == record.high_certificate_digest() &&
            record.authoritative_proposal_id() == record.high_proposal_id() &&
            record.incoming_certificate_digest() == record.low_certificate_digest() &&
            record.incoming_proposal_id() == record.low_proposal_id();
        if ( !authoritative_is_low && !authoritative_is_high )
            return outcome::failure( ConsensusStateStoreError::Integrity );
        return outcome::success();
    }

    outcome::result<void> ConsensusStateStore::ValidateSafety( const SafetyRecord &record,
                                                                const std::string  &key ) const
    {
        if ( record.schema_version() != kSchemaVersion ||
             record.state() != SafetyRecord::SAFETY_VIOLATION || !IsCanonicalHash( record.slot_id() ) ||
             !IsCanonicalHash( record.authoritative_certificate_digest() ) ||
             !IsCanonicalHash( record.authoritative_proposal_id() ) || record.updated_at_ms() == 0 ||
             key != SafetyKey( record.slot_id() ) )
        {
            return outcome::failure( ConsensusStateStoreError::Integrity );
        }
        return outcome::success();
    }

    outcome::result<std::optional<ConsensusStateStore::VoteRecord>> ConsensusStateStore::ReadVoteUnlocked(
        const std::string &validator_id, const std::string &slot_id ) const
    {
        if ( !datastore_ ) return outcome::failure( ConsensusStateStoreError::Storage );
        const auto key = VoteKey( validator_id, slot_id );
        auto raw = datastore_->get( BufferOf( key ) );
        if ( raw.has_error() )
        {
            if ( raw.error() == storage::DatabaseError::NOT_FOUND ) return std::optional<VoteRecord>{};
            return outcome::failure( ConsensusStateStoreError::Storage );
        }
        VoteRecord record;
        if ( !ParseStrict( raw.value().toString(), record ) )
            return outcome::failure( ConsensusStateStoreError::Integrity );
        BOOST_OUTCOME_TRY( ValidateVote( record, key ) );
        return std::optional<VoteRecord>{ std::move( record ) };
    }

    outcome::result<std::optional<ConsensusStateStore::VoteRecord>> ConsensusStateStore::GetVote(
        const std::string &validator_id, const std::string &slot_id ) const
    {
        std::lock_guard lock( mutex_ );
        return ReadVoteUnlocked( validator_id, slot_id );
    }

    outcome::result<std::vector<ConsensusStateStore::VoteRecord>> ConsensusStateStore::ScanVotes() const
    {
        std::lock_guard lock( mutex_ );
        if ( !datastore_ ) return outcome::failure( ConsensusStateStoreError::Storage );
        auto raw = query_( BufferOf( kVotePrefix ) );
        if ( raw.has_error() ) return outcome::failure( ConsensusStateStoreError::Storage );
        BOOST_OUTCOME_TRY( auto records, StrictScan<VoteRecord>( raw.value(), [this]( const auto &r, const auto &k )
                                                                { return ValidateVote( r, k ); } ) );
        std::map<std::pair<std::string, std::string>, uint64_t> active;
        for ( const auto &record : records )
        {
            if ( record.state() != VoteRecord::ACTIVE ) continue;
            auto [it, inserted] = active.emplace( std::make_pair( record.validator_id(), record.slot_id() ),
                                                  record.generation() );
            if ( !inserted && it->second != record.generation() )
                return outcome::failure( ConsensusStateStoreError::Integrity );
        }
        return records;
    }

    outcome::result<void> ConsensusStateStore::PutActiveVote( const VoteRecord &record )
    {
        std::lock_guard lock( mutex_ );
        if ( !datastore_ || record.state() != VoteRecord::ACTIVE )
            return outcome::failure( ConsensusStateStoreError::InvalidArgument );
        BOOST_OUTCOME_TRY( ValidateVote( record, VoteKey( record.validator_id(), record.slot_id() ) ) );
        BOOST_OUTCOME_TRY( auto existing, ReadVoteUnlocked( record.validator_id(), record.slot_id() ) );
        if ( existing )
        {
            if ( existing->state() == VoteRecord::ACTIVE )
            {
                if ( SameVoteIdentityAndBytes( *existing, record ) ) return outcome::success();
                return outcome::failure( ConsensusStateStoreError::Conflict );
            }
            if ( record.generation() <= existing->generation() )
                return outcome::failure( ConsensusStateStoreError::Conflict );
        }
        BOOST_OUTCOME_TRY( auto value, SerializeStrict( record ) );
        auto stored = datastore_->put( BufferOf( VoteKey( record.validator_id(), record.slot_id() ) ), value );
        if ( stored.has_error() ) return outcome::failure( ConsensusStateStoreError::Storage );
        return outcome::success();
    }

    outcome::result<void> ConsensusStateStore::UpdatePublication( const std::string &validator_id,
                                                                  const std::string &slot_id,
                                                                  uint64_t published_at_ms,
                                                                  bool succeeded )
    {
        std::lock_guard lock( mutex_ );
        BOOST_OUTCOME_TRY( auto current, ReadVoteUnlocked( validator_id, slot_id ) );
        if ( !current || current->state() != VoteRecord::ACTIVE || published_at_ms == 0 )
            return outcome::failure( ConsensusStateStoreError::Conflict );
        const auto vote_bytes = current->signed_vote_bytes();
        const auto envelope_bytes = current->outbound_envelope_bytes();
        current->set_publication_count( current->publication_count() + 1 );
        current->set_last_publication_at_ms( published_at_ms );
        current->set_last_publication_succeeded( succeeded );
        if ( current->signed_vote_bytes() != vote_bytes || current->outbound_envelope_bytes() != envelope_bytes )
            return outcome::failure( ConsensusStateStoreError::Integrity );
        BOOST_OUTCOME_TRY( auto value, SerializeStrict( *current ) );
        auto stored = datastore_->put( BufferOf( VoteKey( validator_id, slot_id ) ), value );
        if ( stored.has_error() ) return outcome::failure( ConsensusStateStoreError::Storage );
        return outcome::success();
    }

    outcome::result<void> ConsensusStateStore::RetireVote( const std::string &validator_id,
                                                           const std::string &slot_id,
                                                           uint64_t retired_at_ms )
    {
        std::lock_guard lock( mutex_ );
        BOOST_OUTCOME_TRY( auto current, ReadVoteUnlocked( validator_id, slot_id ) );
        if ( !current ) return outcome::failure( ConsensusStateStoreError::Conflict );
        if ( current->state() == VoteRecord::RETIRED ) return outcome::success();
        if ( retired_at_ms <= current->acceptance_horizon_ms() )
            return outcome::failure( ConsensusStateStoreError::Conflict );
        current->set_state( VoteRecord::RETIRED );
        BOOST_OUTCOME_TRY( auto value, SerializeStrict( *current ) );
        auto batch = datastore_->batch();
        BOOST_OUTCOME_TRY( batch->put( BufferOf( VoteKey( validator_id, slot_id ) ), value ) );
        auto committed = batch->commit();
        if ( committed.has_error() ) return outcome::failure( ConsensusStateStoreError::Storage );
        return outcome::success();
    }

    outcome::result<std::optional<ConsensusStateStore::ProcessRecord>> ConsensusStateStore::ReadProcessUnlocked(
        const std::string &slot_id ) const
    {
        if ( !datastore_ ) return outcome::failure( ConsensusStateStoreError::Storage );
        const auto key = ProcessKey( slot_id );
        auto raw = datastore_->get( BufferOf( key ) );
        if ( raw.has_error() )
        {
            if ( raw.error() == storage::DatabaseError::NOT_FOUND ) return std::optional<ProcessRecord>{};
            return outcome::failure( ConsensusStateStoreError::Storage );
        }
        ProcessRecord record;
        if ( !ParseStrict( raw.value().toString(), record ) )
            return outcome::failure( ConsensusStateStoreError::Integrity );
        BOOST_OUTCOME_TRY( ValidateProcess( record, key ) );
        return std::optional<ProcessRecord>{ std::move( record ) };
    }

    outcome::result<std::optional<ConsensusStateStore::ProcessRecord>> ConsensusStateStore::GetProcess(
        const std::string &slot_id ) const
    {
        std::lock_guard lock( mutex_ );
        return ReadProcessUnlocked( slot_id );
    }

    outcome::result<std::vector<ConsensusStateStore::ProcessRecord>> ConsensusStateStore::ScanProcesses() const
    {
        std::lock_guard lock( mutex_ );
        if ( !datastore_ ) return outcome::failure( ConsensusStateStoreError::Storage );
        auto raw = query_( BufferOf( kProcessPrefix ) );
        if ( raw.has_error() ) return outcome::failure( ConsensusStateStoreError::Storage );
        BOOST_OUTCOME_TRY( auto records, StrictScan<ProcessRecord>( raw.value(), [this]( const auto &r, const auto &k )
                                                                    { return ValidateProcess( r, k ); } ) );
        for ( const auto &record : records )
        {
            auto raw_safety = datastore_->get( BufferOf( SafetyKey( record.slot_id() ) ) );
            if ( raw_safety.has_error() )
            {
                if ( raw_safety.error() == storage::DatabaseError::NOT_FOUND ) continue;
                return outcome::failure( ConsensusStateStoreError::Storage );
            }
            SafetyRecord safety;
            if ( !ParseStrict( raw_safety.value().toString(), safety ) )
                return outcome::failure( ConsensusStateStoreError::Integrity );
            BOOST_OUTCOME_TRY( ValidateSafety( safety, SafetyKey( record.slot_id() ) ) );
            if ( safety.authoritative_certificate_digest() != record.certificate_digest() ||
                 safety.authoritative_proposal_id() != record.proposal_id() )
                return outcome::failure( ConsensusStateStoreError::Integrity );
        }
        return records;
    }

    outcome::result<void> ConsensusStateStore::PutPendingProcess( const ProcessRecord &record )
    {
        std::lock_guard lock( mutex_ );
        if ( !datastore_ || record.state() != ProcessRecord::PENDING )
            return outcome::failure( ConsensusStateStoreError::InvalidArgument );
        BOOST_OUTCOME_TRY( ValidateProcess( record, ProcessKey( record.slot_id() ) ) );
        BOOST_OUTCOME_TRY( auto existing, ReadProcessUnlocked( record.slot_id() ) );
        if ( existing )
        {
            if ( existing->certificate_digest() == record.certificate_digest() &&
                 existing->proposal_id() == record.proposal_id() && existing->winner_id() == record.winner_id() )
                return outcome::success();
            return outcome::failure( ConsensusStateStoreError::Conflict );
        }
        BOOST_OUTCOME_TRY( auto value, SerializeStrict( record ) );
        auto stored = datastore_->put( BufferOf( ProcessKey( record.slot_id() ) ), value );
        if ( stored.has_error() ) return outcome::failure( ConsensusStateStoreError::Storage );
        return outcome::success();
    }

    outcome::result<void> ConsensusStateStore::MarkProcessing( const std::string &slot_id,
                                                               uint64_t lease_until_ms,
                                                               uint64_t updated_at_ms )
    {
        std::lock_guard lock( mutex_ );
        BOOST_OUTCOME_TRY( auto current, ReadProcessUnlocked( slot_id ) );
        if ( !current || current->state() == ProcessRecord::COMPLETE || lease_until_ms <= updated_at_ms )
            return outcome::failure( ConsensusStateStoreError::Conflict );
        current->set_state( ProcessRecord::PROCESSING );
        current->set_attempt_count( current->attempt_count() + 1 );
        current->set_lease_until_ms( lease_until_ms );
        current->set_updated_at_ms( updated_at_ms );
        BOOST_OUTCOME_TRY( auto value, SerializeStrict( *current ) );
        auto stored = datastore_->put( BufferOf( ProcessKey( slot_id ) ), value );
        if ( stored.has_error() ) return outcome::failure( ConsensusStateStoreError::Storage );
        return outcome::success();
    }

    outcome::result<void> ConsensusStateStore::MarkComplete( const std::string &slot_id, uint64_t updated_at_ms )
    {
        std::lock_guard lock( mutex_ );
        BOOST_OUTCOME_TRY( auto current, ReadProcessUnlocked( slot_id ) );
        if ( !current ) return outcome::failure( ConsensusStateStoreError::Conflict );
        if ( current->state() == ProcessRecord::COMPLETE ) return outcome::success();
        current->set_state( ProcessRecord::COMPLETE );
        current->set_lease_until_ms( 0 );
        current->set_updated_at_ms( updated_at_ms );
        BOOST_OUTCOME_TRY( auto value, SerializeStrict( *current ) );
        auto stored = datastore_->put( BufferOf( ProcessKey( slot_id ) ), value );
        if ( stored.has_error() ) return outcome::failure( ConsensusStateStoreError::Storage );
        return outcome::success();
    }

    outcome::result<void> ConsensusStateStore::RestorePending( const std::string &slot_id, uint64_t updated_at_ms )
    {
        std::lock_guard lock( mutex_ );
        BOOST_OUTCOME_TRY( auto current, ReadProcessUnlocked( slot_id ) );
        if ( !current || current->state() == ProcessRecord::COMPLETE || updated_at_ms == 0 )
            return outcome::failure( ConsensusStateStoreError::Conflict );
        current->set_state( ProcessRecord::PENDING );
        current->set_lease_until_ms( 0 );
        current->set_updated_at_ms( updated_at_ms );
        BOOST_OUTCOME_TRY( auto value, SerializeStrict( *current ) );
        auto stored = datastore_->put( BufferOf( ProcessKey( slot_id ) ), value );
        if ( stored.has_error() ) return outcome::failure( ConsensusStateStoreError::Storage );
        return outcome::success();
    }

    outcome::result<std::vector<ConsensusStateStore::ConflictRecord>> ConsensusStateStore::ScanConflicts() const
    {
        std::lock_guard lock( mutex_ );
        if ( !datastore_ ) return outcome::failure( ConsensusStateStoreError::Storage );
        auto raw = query_( BufferOf( kConflictPrefix ) );
        if ( raw.has_error() ) return outcome::failure( ConsensusStateStoreError::Storage );
        BOOST_OUTCOME_TRY( auto records, StrictScan<ConflictRecord>( raw.value(), [this]( const auto &r, const auto &k )
                                                                     { return ValidateConflict( r, k ); } ) );
        for ( const auto &record : records )
        {
            auto raw_safety = datastore_->get( BufferOf( SafetyKey( record.slot_id() ) ) );
            if ( raw_safety.has_error() )
                return outcome::failure( raw_safety.error() == storage::DatabaseError::NOT_FOUND
                                             ? ConsensusStateStoreError::Integrity
                                             : ConsensusStateStoreError::Storage );
            SafetyRecord safety;
            if ( !ParseStrict( raw_safety.value().toString(), safety ) )
                return outcome::failure( ConsensusStateStoreError::Integrity );
            BOOST_OUTCOME_TRY( ValidateSafety( safety, SafetyKey( record.slot_id() ) ) );
            const bool low_matches = safety.authoritative_certificate_digest() == record.low_certificate_digest() &&
                                     safety.authoritative_proposal_id() == record.low_proposal_id();
            const bool high_matches = safety.authoritative_certificate_digest() == record.high_certificate_digest() &&
                                      safety.authoritative_proposal_id() == record.high_proposal_id();
            if ( !low_matches && !high_matches )
                return outcome::failure( ConsensusStateStoreError::Integrity );
        }
        return records;
    }

    outcome::result<std::vector<ConsensusStateStore::SafetyRecord>> ConsensusStateStore::ScanSafety() const
    {
        std::lock_guard lock( mutex_ );
        if ( !datastore_ ) return outcome::failure( ConsensusStateStoreError::Storage );
        auto raw = query_( BufferOf( kSafetyPrefix ) );
        if ( raw.has_error() ) return outcome::failure( ConsensusStateStoreError::Storage );
        BOOST_OUTCOME_TRY( auto records, StrictScan<SafetyRecord>( raw.value(), [this]( const auto &r, const auto &k )
                                                                   { return ValidateSafety( r, k ); } ) );
        for ( const auto &record : records )
        {
            auto conflicts = query_( BufferOf( std::string( kConflictPrefix ) + record.slot_id() + "/" ) );
            if ( conflicts.has_error() ) return outcome::failure( ConsensusStateStoreError::Storage );
            bool found_authoritative_pair = false;
            for ( const auto &[key, value] : conflicts.value() )
            {
                ConflictRecord conflict;
                if ( !ParseStrict( value.toString(), conflict ) )
                    return outcome::failure( ConsensusStateStoreError::Integrity );
                BOOST_OUTCOME_TRY( ValidateConflict( conflict, std::string( key.toString() ) ) );
                found_authoritative_pair =
                    found_authoritative_pair ||
                    ( record.authoritative_certificate_digest() == conflict.low_certificate_digest() &&
                      record.authoritative_proposal_id() == conflict.low_proposal_id() ) ||
                    ( record.authoritative_certificate_digest() == conflict.high_certificate_digest() &&
                      record.authoritative_proposal_id() == conflict.high_proposal_id() );
            }
            if ( !found_authoritative_pair ) return outcome::failure( ConsensusStateStoreError::Integrity );
        }
        return records;
    }

    outcome::result<ConsensusStateStore::ConflictRecord> ConsensusStateStore::RecordConflictAndSafety(
        ConflictRecord conflict, SafetyRecord safety )
    {
        std::lock_guard lock( mutex_ );
        if ( !datastore_ || conflict.slot_id() != safety.slot_id() )
            return outcome::failure( ConsensusStateStoreError::InvalidArgument );
        if ( conflict.low_certificate_digest() > conflict.high_certificate_digest() )
        {
            conflict.mutable_low_certificate_digest()->swap( *conflict.mutable_high_certificate_digest() );
            conflict.mutable_low_proposal_id()->swap( *conflict.mutable_high_proposal_id() );
        }
        const auto conflict_key = ConflictKey( conflict.slot_id(),
                                               conflict.low_certificate_digest(),
                                               conflict.high_certificate_digest() );
        BOOST_OUTCOME_TRY( ValidateConflict( conflict, conflict_key ) );
        BOOST_OUTCOME_TRY( ValidateSafety( safety, SafetyKey( safety.slot_id() ) ) );
        const bool safety_is_low = safety.authoritative_certificate_digest() == conflict.low_certificate_digest() &&
                                   safety.authoritative_proposal_id() == conflict.low_proposal_id();
        const bool safety_is_high = safety.authoritative_certificate_digest() == conflict.high_certificate_digest() &&
                                    safety.authoritative_proposal_id() == conflict.high_proposal_id();
        if ( !safety_is_low && !safety_is_high )
            return outcome::failure( ConsensusStateStoreError::Conflict );

        auto existing = datastore_->get( BufferOf( conflict_key ) );
        if ( existing.has_value() )
        {
            ConflictRecord prior;
            if ( !ParseStrict( existing.value().toString(), prior ) )
                return outcome::failure( ConsensusStateStoreError::Integrity );
            BOOST_OUTCOME_TRY( ValidateConflict( prior, conflict_key ) );
            if ( prior.low_proposal_id() != conflict.low_proposal_id() ||
                 prior.high_proposal_id() != conflict.high_proposal_id() ||
                 prior.authoritative_certificate_digest() != conflict.authoritative_certificate_digest() ||
                 prior.authoritative_proposal_id() != conflict.authoritative_proposal_id() ||
                 prior.incoming_certificate_digest() != conflict.incoming_certificate_digest() ||
                 prior.incoming_proposal_id() != conflict.incoming_proposal_id() )
                return outcome::failure( ConsensusStateStoreError::Conflict );
            prior.set_sources_bitset( prior.sources_bitset() | conflict.sources_bitset() );
            prior.set_last_seen_at_ms( std::max( prior.last_seen_at_ms(), conflict.last_seen_at_ms() ) );
            prior.set_observation_count( prior.observation_count() + 1 );
            conflict = std::move( prior );
        }
        else if ( existing.error() != storage::DatabaseError::NOT_FOUND )
        {
            return outcome::failure( ConsensusStateStoreError::Storage );
        }

        auto existing_safety = datastore_->get( BufferOf( SafetyKey( safety.slot_id() ) ) );
        if ( existing_safety.has_value() )
        {
            SafetyRecord prior;
            if ( !ParseStrict( existing_safety.value().toString(), prior ) )
                return outcome::failure( ConsensusStateStoreError::Integrity );
            BOOST_OUTCOME_TRY( ValidateSafety( prior, SafetyKey( safety.slot_id() ) ) );
            if ( prior.authoritative_certificate_digest() != safety.authoritative_certificate_digest() ||
                 prior.authoritative_proposal_id() != safety.authoritative_proposal_id() )
                return outcome::failure( ConsensusStateStoreError::Conflict );
            safety.set_updated_at_ms( std::max( prior.updated_at_ms(), safety.updated_at_ms() ) );
        }
        else if ( existing_safety.error() != storage::DatabaseError::NOT_FOUND )
        {
            return outcome::failure( ConsensusStateStoreError::Storage );
        }

        BOOST_OUTCOME_TRY( auto conflict_value, SerializeStrict( conflict ) );
        BOOST_OUTCOME_TRY( auto safety_value, SerializeStrict( safety ) );
        auto batch = datastore_->batch();
        BOOST_OUTCOME_TRY( batch->put( BufferOf( conflict_key ), conflict_value ) );
        BOOST_OUTCOME_TRY( batch->put( BufferOf( SafetyKey( safety.slot_id() ) ), safety_value ) );
        auto committed = batch->commit();
        if ( committed.has_error() ) return outcome::failure( ConsensusStateStoreError::Storage );
        return conflict;
    }

    outcome::result<void> ConsensusStateStore::ValidateBurnReservation( const BurnReservationRecord &record,
                                                                         const std::string &key ) const
    {
        const BurnOutpoint outpoint{ record.source_chain(), record.burn_hash(), record.receipt_log_index() };
        const bool reserved = record.state() == BurnReservationRecord::RESERVED;
        const bool finalized = record.state() == BurnReservationRecord::FINALIZED_PENDING_APPLICATION ||
                               record.state() == BurnReservationRecord::CONSUMED ||
                               record.state() == BurnReservationRecord::SAFETY_ERROR;
        if ( record.schema_version() != kSchemaVersion || ( !reserved && !finalized ) ||
             !IsCanonicalHash( record.slot_id() ) || !IsCanonicalChain( record.source_chain() ) ||
             !IsNonzeroCanonicalHash( record.burn_hash() ) || !IsCanonicalHash( record.generation() ) ||
             record.slot_id() != BurnSlotFor( outpoint ) || key != BurnSlotKey( record.slot_id() ) ||
             record.created_at_ms() == 0 || record.updated_at_ms() < record.created_at_ms() ||
             ( reserved && ( record.candidate_acceptance_horizon_ms() < record.created_at_ms() ||
                             !record.certificate_digest().empty() || !record.proposal_id().empty() ||
                             !record.winner_id().empty() || !record.safety_error().empty() ) ) ||
             ( finalized && ( !IsCanonicalHash( record.certificate_digest() ) ||
                              !IsCanonicalHash( record.proposal_id() ) || !IsCanonicalHash( record.winner_id() ) ) ) ||
             ( record.state() == BurnReservationRecord::SAFETY_ERROR ) != !record.safety_error().empty() )
        {
            return outcome::failure( ConsensusStateStoreError::Integrity );
        }
        return outcome::success();
    }

    outcome::result<void> ConsensusStateStore::ValidateBurnOutpointIndex( const BurnOutpointIndex &record,
                                                                          const std::string &key ) const
    {
        const BurnOutpoint outpoint{ record.source_chain(), record.burn_hash(), record.receipt_log_index() };
        if ( record.schema_version() != kSchemaVersion || !IsCanonicalHash( record.slot_id() ) ||
             !IsCanonicalChain( record.source_chain() ) || !IsNonzeroCanonicalHash( record.burn_hash() ) ||
             !IsCanonicalHash( record.generation() ) || record.slot_id() != BurnSlotFor( outpoint ) ||
             key != BurnOutpointKey( outpoint ) )
        {
            return outcome::failure( ConsensusStateStoreError::Integrity );
        }
        return outcome::success();
    }

    outcome::result<std::optional<ConsensusStateStore::BurnOutpointIndex>>
    ConsensusStateStore::ReadBurnOutpointIndexUnlocked( const BurnOutpoint &outpoint ) const
    {
        if ( !datastore_ ) return outcome::failure( ConsensusStateStoreError::Storage );
        const auto key = BurnOutpointKey( outpoint );
        auto raw = datastore_->get( BufferOf( key ) );
        if ( raw.has_error() )
        {
            if ( raw.error() == storage::DatabaseError::NOT_FOUND ) return std::optional<BurnOutpointIndex>{};
            return outcome::failure( ConsensusStateStoreError::Storage );
        }
        BurnOutpointIndex index;
        if ( !ParseStrict( raw.value().toString(), index ) )
            return outcome::failure( ConsensusStateStoreError::Integrity );
        BOOST_OUTCOME_TRY( ValidateBurnOutpointIndex( index, key ) );
        return std::optional<BurnOutpointIndex>{ std::move( index ) };
    }

    outcome::result<void> ConsensusStateStore::ValidateBurnReciprocalUnlocked(
        const BurnReservationRecord &record ) const
    {
        const BurnOutpoint outpoint{ record.source_chain(), record.burn_hash(), record.receipt_log_index() };
        BOOST_OUTCOME_TRY( auto index, ReadBurnOutpointIndexUnlocked( outpoint ) );
        if ( !index || index->slot_id() != record.slot_id() || index->generation() != record.generation() ||
             index->source_chain() != record.source_chain() || index->burn_hash() != record.burn_hash() ||
             index->receipt_log_index() != record.receipt_log_index() )
            return outcome::failure( ConsensusStateStoreError::Integrity );
        return outcome::success();
    }

    outcome::result<std::optional<ConsensusStateStore::BurnReservationRecord>>
    ConsensusStateStore::ReadBurnReservationUnlocked( const std::string &slot_id ) const
    {
        if ( !datastore_ ) return outcome::failure( ConsensusStateStoreError::Storage );
        const auto key = BurnSlotKey( slot_id );
        auto raw = datastore_->get( BufferOf( key ) );
        if ( raw.has_error() )
        {
            if ( raw.error() == storage::DatabaseError::NOT_FOUND ) return std::optional<BurnReservationRecord>{};
            return outcome::failure( ConsensusStateStoreError::Storage );
        }
        BurnReservationRecord record;
        if ( !ParseStrict( raw.value().toString(), record ) )
            return outcome::failure( ConsensusStateStoreError::Integrity );
        BOOST_OUTCOME_TRY( ValidateBurnReservation( record, key ) );
        BOOST_OUTCOME_TRY( ValidateBurnReciprocalUnlocked( record ) );
        return std::optional<BurnReservationRecord>{ std::move( record ) };
    }

    outcome::result<std::optional<ConsensusStateStore::BurnReservationRecord>>
    ConsensusStateStore::GetBurnReservation( const std::string &slot_id ) const
    {
        std::lock_guard lock( mutex_ );
        if ( !IsCanonicalHash( slot_id ) ) return outcome::failure( ConsensusStateStoreError::InvalidArgument );
        return ReadBurnReservationUnlocked( slot_id );
    }

    outcome::result<std::optional<ConsensusStateStore::BurnReservationRecord>>
    ConsensusStateStore::GetBurnReservation( const BurnOutpoint &outpoint ) const
    {
        std::lock_guard lock( mutex_ );
        if ( !IsCanonicalChain( outpoint.source_chain ) || !IsNonzeroCanonicalHash( outpoint.burn_hash ) )
            return outcome::failure( ConsensusStateStoreError::InvalidArgument );
        BOOST_OUTCOME_TRY( auto index, ReadBurnOutpointIndexUnlocked( outpoint ) );
        if ( !index ) return std::optional<BurnReservationRecord>{};
        BOOST_OUTCOME_TRY( auto record, ReadBurnReservationUnlocked( index->slot_id() ) );
        if ( !record ) return outcome::failure( ConsensusStateStoreError::Integrity );
        return record;
    }

    outcome::result<std::vector<ConsensusStateStore::BurnReservationRecord>>
    ConsensusStateStore::ScanBurnReservations() const
    {
        std::lock_guard lock( mutex_ );
        if ( !datastore_ ) return outcome::failure( ConsensusStateStoreError::Storage );
        auto raw_slots = query_( BufferOf( kBurnSlotPrefix ) );
        auto raw_indexes = query_( BufferOf( kBurnOutpointPrefix ) );
        if ( raw_slots.has_error() || raw_indexes.has_error() )
            return outcome::failure( ConsensusStateStoreError::Storage );
        BOOST_OUTCOME_TRY( auto records,
                           StrictScan<BurnReservationRecord>( raw_slots.value(), [this]( const auto &r, const auto &k )
                                                              { return ValidateBurnReservation( r, k ); } ) );
        BOOST_OUTCOME_TRY( auto indexes,
                           StrictScan<BurnOutpointIndex>( raw_indexes.value(), [this]( const auto &r, const auto &k )
                                                         { return ValidateBurnOutpointIndex( r, k ); } ) );
        if ( records.size() != indexes.size() ) return outcome::failure( ConsensusStateStoreError::Integrity );
        for ( const auto &record : records )
        {
            BOOST_OUTCOME_TRY( ValidateBurnReciprocalUnlocked( record ) );
        }
        for ( const auto &index : indexes )
        {
            BOOST_OUTCOME_TRY( auto record, ReadBurnReservationUnlocked( index.slot_id() ) );
            if ( !record || record->generation() != index.generation() )
                return outcome::failure( ConsensusStateStoreError::Integrity );
        }
        return records;
    }

    outcome::result<ConsensusStateStore::BurnReservationResult>
    ConsensusStateStore::CreateOrJoinBurnReservation( const std::string &slot_id,
                                                       const BurnOutpoint &outpoint,
                                                       uint64_t candidate_acceptance_horizon_ms,
                                                       uint64_t now_ms )
    {
        std::lock_guard lock( mutex_ );
        if ( !datastore_ || !IsCanonicalChain( outpoint.source_chain ) ||
             !IsNonzeroCanonicalHash( outpoint.burn_hash ) || !IsCanonicalHash( slot_id ) || now_ms == 0 ||
             candidate_acceptance_horizon_ms < now_ms )
            return outcome::failure( ConsensusStateStoreError::InvalidArgument );
        if ( slot_id != BurnSlotFor( outpoint ) )
            return outcome::failure( ConsensusStateStoreError::Conflict );

        BOOST_OUTCOME_TRY( auto current, ReadBurnReservationUnlocked( slot_id ) );
        BOOST_OUTCOME_TRY( auto indexed, ReadBurnOutpointIndexUnlocked( outpoint ) );
        if ( current )
        {
            if ( !SameOutpoint( *current, outpoint ) || !indexed || indexed->slot_id() != slot_id ||
                 indexed->generation() != current->generation() )
                return outcome::failure( ConsensusStateStoreError::Conflict );
            if ( current->state() != BurnReservationRecord::RESERVED )
                return outcome::failure( ConsensusStateStoreError::Conflict );
            if ( candidate_acceptance_horizon_ms > current->candidate_acceptance_horizon_ms() )
            {
                current->set_candidate_acceptance_horizon_ms( candidate_acceptance_horizon_ms );
                current->set_updated_at_ms( std::max( now_ms, current->updated_at_ms() ) );
                BOOST_OUTCOME_TRY( auto value, SerializeStrict( *current ) );
                auto stored = datastore_->put( BufferOf( BurnSlotKey( slot_id ) ), value );
                if ( stored.has_error() ) return outcome::failure( ConsensusStateStoreError::Storage );
            }
            return BurnReservationResult{ *current, false };
        }
        if ( indexed ) return outcome::failure( ConsensusStateStoreError::Conflict );

        BOOST_OUTCOME_TRY( auto generation, RandomGeneration() );
        BurnReservationRecord record;
        record.set_schema_version( kSchemaVersion );
        record.set_state( BurnReservationRecord::RESERVED );
        record.set_slot_id( slot_id );
        record.set_source_chain( outpoint.source_chain );
        record.set_burn_hash( outpoint.burn_hash );
        record.set_receipt_log_index( outpoint.receipt_log_index );
        record.set_generation( generation );
        record.set_candidate_acceptance_horizon_ms( candidate_acceptance_horizon_ms );
        record.set_created_at_ms( now_ms );
        record.set_updated_at_ms( now_ms );
        BurnOutpointIndex index;
        index.set_schema_version( kSchemaVersion );
        index.set_slot_id( slot_id );
        index.set_source_chain( outpoint.source_chain );
        index.set_burn_hash( outpoint.burn_hash );
        index.set_receipt_log_index( outpoint.receipt_log_index );
        index.set_generation( generation );
        BOOST_OUTCOME_TRY( ValidateBurnReservation( record, BurnSlotKey( slot_id ) ) );
        BOOST_OUTCOME_TRY( ValidateBurnOutpointIndex( index, BurnOutpointKey( outpoint ) ) );
        BOOST_OUTCOME_TRY( auto record_value, SerializeStrict( record ) );
        BOOST_OUTCOME_TRY( auto index_value, SerializeStrict( index ) );
        auto batch = datastore_->batch();
        BOOST_OUTCOME_TRY( batch->put( BufferOf( BurnSlotKey( slot_id ) ), record_value ) );
        BOOST_OUTCOME_TRY( batch->put( BufferOf( BurnOutpointKey( outpoint ) ), index_value ) );
        auto committed = commit_( *batch );
        if ( committed.has_error() ) return outcome::failure( ConsensusStateStoreError::Storage );
        return BurnReservationResult{ std::move( record ), true };
    }

    outcome::result<ConsensusStateStore::BurnReservationRecord> ConsensusStateStore::FinalizeBurnReservation(
        const std::string &slot_id,
        const BurnOutpoint &outpoint,
        const std::string &certificate_digest,
        const std::string &proposal_id,
        const std::string &winner_id,
        uint64_t now_ms )
    {
        std::lock_guard lock( mutex_ );
        if ( !datastore_ || !IsCanonicalChain( outpoint.source_chain ) ||
             !IsNonzeroCanonicalHash( outpoint.burn_hash ) || !IsCanonicalHash( slot_id ) ||
             !IsCanonicalHash( certificate_digest ) ||
             !IsCanonicalHash( proposal_id ) || !IsCanonicalHash( winner_id ) || now_ms == 0 )
            return outcome::failure( ConsensusStateStoreError::InvalidArgument );
        if ( slot_id != BurnSlotFor( outpoint ) )
            return outcome::failure( ConsensusStateStoreError::Conflict );
        BOOST_OUTCOME_TRY( auto current, ReadBurnReservationUnlocked( slot_id ) );
        BOOST_OUTCOME_TRY( auto indexed, ReadBurnOutpointIndexUnlocked( outpoint ) );
        if ( current )
        {
            if ( !SameOutpoint( *current, outpoint ) || !indexed || indexed->generation() != current->generation() )
                return outcome::failure( ConsensusStateStoreError::Conflict );
            if ( current->state() != BurnReservationRecord::RESERVED )
            {
                if ( SameFinality( *current, certificate_digest, proposal_id, winner_id ) ) return *current;
                return outcome::failure( ConsensusStateStoreError::Conflict );
            }
        }
        else
        {
            if ( indexed ) return outcome::failure( ConsensusStateStoreError::Conflict );
            BOOST_OUTCOME_TRY( auto generation, RandomGeneration() );
            current.emplace();
            current->set_schema_version( kSchemaVersion );
            current->set_slot_id( slot_id );
            current->set_source_chain( outpoint.source_chain );
            current->set_burn_hash( outpoint.burn_hash );
            current->set_receipt_log_index( outpoint.receipt_log_index );
            current->set_generation( generation );
            current->set_created_at_ms( now_ms );
            indexed.emplace();
            indexed->set_schema_version( kSchemaVersion );
            indexed->set_slot_id( slot_id );
            indexed->set_source_chain( outpoint.source_chain );
            indexed->set_burn_hash( outpoint.burn_hash );
            indexed->set_receipt_log_index( outpoint.receipt_log_index );
            indexed->set_generation( generation );
        }
        current->set_state( BurnReservationRecord::FINALIZED_PENDING_APPLICATION );
        current->set_certificate_digest( certificate_digest );
        current->set_proposal_id( proposal_id );
        current->set_winner_id( winner_id );
        current->set_updated_at_ms( std::max( now_ms, current->updated_at_ms() ) );
        BOOST_OUTCOME_TRY( ValidateBurnReservation( *current, BurnSlotKey( slot_id ) ) );
        BOOST_OUTCOME_TRY( auto record_value, SerializeStrict( *current ) );
        BOOST_OUTCOME_TRY( auto index_value, SerializeStrict( *indexed ) );
        auto batch = datastore_->batch();
        BOOST_OUTCOME_TRY( batch->put( BufferOf( BurnSlotKey( slot_id ) ), record_value ) );
        BOOST_OUTCOME_TRY( batch->put( BufferOf( BurnOutpointKey( outpoint ) ), index_value ) );
        auto committed = commit_( *batch );
        if ( committed.has_error() ) return outcome::failure( ConsensusStateStoreError::Storage );
        return *current;
    }

    outcome::result<ConsensusStateStore::BurnReservationRecord>
    ConsensusStateStore::MarkBurnReservationSafetyError( const std::string &slot_id,
                                                          const std::string &expected_generation,
                                                          const std::string &certificate_digest,
                                                          const std::string &proposal_id,
                                                          const std::string &winner_id,
                                                          const std::string &diagnostic,
                                                          uint64_t now_ms )
    {
        std::lock_guard lock( mutex_ );
        if ( diagnostic.empty() || now_ms == 0 ) return outcome::failure( ConsensusStateStoreError::InvalidArgument );
        BOOST_OUTCOME_TRY( auto current, ReadBurnReservationUnlocked( slot_id ) );
        if ( !current || current->generation() != expected_generation ||
             !SameFinality( *current, certificate_digest, proposal_id, winner_id ) )
            return outcome::failure( ConsensusStateStoreError::Conflict );
        if ( current->state() == BurnReservationRecord::SAFETY_ERROR )
        {
            if ( current->safety_error() == diagnostic ) return *current;
            return outcome::failure( ConsensusStateStoreError::Conflict );
        }
        if ( current->state() != BurnReservationRecord::FINALIZED_PENDING_APPLICATION )
            return outcome::failure( ConsensusStateStoreError::Conflict );
        current->set_state( BurnReservationRecord::SAFETY_ERROR );
        current->set_safety_error( diagnostic );
        current->set_updated_at_ms( std::max( now_ms, current->updated_at_ms() ) );
        BOOST_OUTCOME_TRY( ValidateBurnReservation( *current, BurnSlotKey( slot_id ) ) );
        BOOST_OUTCOME_TRY( auto value, SerializeStrict( *current ) );
        auto stored = datastore_->put( BufferOf( BurnSlotKey( slot_id ) ), value );
        if ( stored.has_error() ) return outcome::failure( ConsensusStateStoreError::Storage );
        return *current;
    }

    outcome::result<ConsensusStateStore::BurnReservationRecord>
    ConsensusStateStore::PrepareConsumedBurnReservation( storage::BufferBatch &batch,
                                                          const std::string &slot_id,
                                                          const BurnOutpoint &outpoint,
                                                          const std::string &expected_generation,
                                                          const std::string &certificate_digest,
                                                          const std::string &proposal_id,
                                                          const std::string &winner_id,
                                                          uint64_t now_ms )
    {
        std::lock_guard lock( mutex_ );
        return PrepareConsumedBurnReservationUnlocked(
            batch,
            FinalizedReservationIdentity{ slot_id, outpoint, expected_generation,
                                          certificate_digest, proposal_id, winner_id },
            now_ms );
    }

    outcome::result<ConsensusStateStore::BurnReservationRecord>
    ConsensusStateStore::PrepareConsumedBurnReservationUnlocked(
        storage::BufferBatch &batch,
        const FinalizedReservationIdentity &identity,
        uint64_t now_ms )
    {
        BOOST_OUTCOME_TRY( auto current, ReadBurnReservationUnlocked( identity.slot_id ) );
        if ( !current || !SameOutpoint( *current, identity.outpoint ) ||
             current->generation() != identity.generation ||
             !SameFinality( *current, identity.certificate_digest,
                            identity.proposal_id, identity.winner_id ) )
            return outcome::failure( ConsensusStateStoreError::Conflict );
        if ( current->state() == BurnReservationRecord::CONSUMED ) return *current;
        if ( current->state() != BurnReservationRecord::FINALIZED_PENDING_APPLICATION || now_ms == 0 )
            return outcome::failure( ConsensusStateStoreError::Conflict );
        current->set_state( BurnReservationRecord::CONSUMED );
        current->set_updated_at_ms( std::max( now_ms, current->updated_at_ms() ) );
        BOOST_OUTCOME_TRY( ValidateBurnReservation( *current, BurnSlotKey( identity.slot_id ) ) );
        BOOST_OUTCOME_TRY( auto value, SerializeStrict( *current ) );
        BOOST_OUTCOME_TRY( batch.put( BufferOf( BurnSlotKey( identity.slot_id ) ), value ) );
        return *current;
    }

    outcome::result<ConsensusStateStore::BurnDeleteResult>
    ConsensusStateStore::DeleteReservedBurnReservation( const std::string &slot_id,
                                                         const std::string &expected_generation,
                                                         std::optional<uint64_t> expected_candidate_horizon_ms )
    {
        std::lock_guard lock( mutex_ );
        if ( !IsCanonicalHash( slot_id ) || !IsCanonicalHash( expected_generation ) )
            return outcome::failure( ConsensusStateStoreError::InvalidArgument );
        BOOST_OUTCOME_TRY( auto current, ReadBurnReservationUnlocked( slot_id ) );
        if ( !current ) return BurnDeleteResult::NotFound;
        if ( current->generation() != expected_generation ) return BurnDeleteResult::GenerationMismatch;
        if ( current->state() != BurnReservationRecord::RESERVED )
            return outcome::failure( ConsensusStateStoreError::Conflict );
        if ( expected_candidate_horizon_ms.has_value() &&
             current->candidate_acceptance_horizon_ms() != expected_candidate_horizon_ms.value() )
            return BurnDeleteResult::GenerationMismatch;
        const BurnOutpoint outpoint{ current->source_chain(), current->burn_hash(), current->receipt_log_index() };
        BOOST_OUTCOME_TRY( auto indexed, ReadBurnOutpointIndexUnlocked( outpoint ) );
        if ( !indexed || indexed->slot_id() != slot_id || indexed->generation() != expected_generation )
            return outcome::failure( ConsensusStateStoreError::Integrity );
        auto batch = datastore_->batch();
        BOOST_OUTCOME_TRY( batch->remove( BufferOf( BurnSlotKey( slot_id ) ) ) );
        BOOST_OUTCOME_TRY( batch->remove( BufferOf( BurnOutpointKey( outpoint ) ) ) );
        auto committed = commit_( *batch );
        if ( committed.has_error() ) return outcome::failure( ConsensusStateStoreError::Storage );
        return BurnDeleteResult::Deleted;
    }

    outcome::result<void> ConsensusStateStore::ApplyFinalizedReservationBatch(
        const FinalizedReservationIdentity &identity,
        const std::shared_ptr<storage::rocksdb> &participant_datastore,
        FinalizedBatchParticipant participant )
    {
        if ( !datastore_ || !participant_datastore || !participant )
            return outcome::failure( ConsensusStateStoreError::InvalidArgument );

        // Shared-object identity is the locking authority. A separately opened
        // rocksdb object naming the same path is deliberately not equivalent.
        if ( datastore_ != participant_datastore )
            return outcome::failure( ConsensusStateStoreError::DatastoreIdentity );

        std::lock_guard lock( mutex_ );
        BOOST_OUTCOME_TRY( auto current, ReadBurnReservationUnlocked( identity.slot_id ) );
        if ( !current ) return outcome::failure( ConsensusStateStoreError::Integrity );
        if ( ( current->state() != BurnReservationRecord::FINALIZED_PENDING_APPLICATION &&
               current->state() != BurnReservationRecord::CONSUMED ) ||
             !SameOutpoint( *current, identity.outpoint ) ||
             current->generation() != identity.generation ||
             current->certificate_digest() != identity.certificate_digest ||
             current->proposal_id() != identity.proposal_id ||
             current->winner_id() != identity.winner_id )
            return outcome::failure( ConsensusStateStoreError::Conflict );

        auto batch = datastore_->batch();
        if ( !batch ) return outcome::failure( ConsensusStateStoreError::Storage );
        // Preserve the live pre-stage state for replay classification while
        // staging Consumed into the same participant-owned physical batch.
        const auto live_state = *current;
        BOOST_OUTCOME_TRY( PrepareConsumedBurnReservationUnlocked(
            *batch, identity, current->updated_at_ms() ) );
        return participant( *batch, live_state );
    }
} // namespace sgns
