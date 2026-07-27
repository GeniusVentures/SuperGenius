#include "blockchain/ConsensusStateStore.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <utility>

#include "base/hexutil.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"
#include "crypto/hasher.hpp"
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
} // namespace sgns
