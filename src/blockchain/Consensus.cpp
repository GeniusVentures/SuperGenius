/**
 * @file       Consensus.cpp
 * @brief      Consensus proposal/vote/certificate helpers.
 * @date       2025-10-16
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "blockchain/Consensus.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <regex>
#include <set>
#include <system_error>
#include <boost/format.hpp>

#include <gsl/span>
#include <utility>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <google/protobuf/message.h>

#include "base/hexutil.hpp"
#include "base/sgns_version.hpp"
#include "crypto/hasher.hpp"
#include "account/GeniusAccount.hpp"
#include "account/GeniusTransaction.hpp"
#include "blockchain/ConsensusAuth.hpp"
#include "storage/database_error.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns, ConsensusManager::CertificateStoreError, error )
{
    using Error = sgns::ConsensusManager::CertificateStoreError;
    switch ( error )
    {
        case Error::NotFound:
            return "Certificate record not found";
        case Error::IntegrityError:
            return "Certificate store integrity error";
        case Error::StorageError:
            return "Certificate store operational error";
        case Error::Conflict:
            return "Certificate store conflict";
        case Error::InvalidInput:
            return "Invalid canonical certificate lookup input";
        case Error::InvalidCertificate:
            return "Invalid certificate";
    }
    return "Unknown certificate store error";
}

namespace sgns
{
    namespace
    {
        bool IsCanonicalHash( std::string_view value )
        {
            return value.size() == 64 &&
                   std::all_of( value.begin(),
                                value.end(),
                                []( unsigned char c )
                                { return ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'f' ); } );
        }

        std::string PublicPayloadDigest( std::string_view bytes )
        {
            const auto digest = crypto::sha2_256( bytes.data(), bytes.size() );
            return base::hex_lower( gsl::span<const uint8_t>( digest.data(), digest.size() ) );
        }

        bool HasUnknownFieldsRecursively( const google::protobuf::Message &message )
        {
            const auto *reflection = message.GetReflection();
            const auto *descriptor = message.GetDescriptor();
            if ( reflection == nullptr || descriptor == nullptr ||
                 !reflection->GetUnknownFields( message ).empty() )
            {
                return true;
            }

            for ( int i = 0; i < descriptor->field_count(); ++i )
            {
                const auto *field = descriptor->field( i );
                if ( field->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE )
                {
                    continue;
                }
                if ( field->is_repeated() )
                {
                    const int count = reflection->FieldSize( message, field );
                    for ( int index = 0; index < count; ++index )
                    {
                        if ( HasUnknownFieldsRecursively(
                                 reflection->GetRepeatedMessage( message, field, index ) ) )
                        {
                            return true;
                        }
                    }
                }
                else if ( reflection->HasField( message, field ) &&
                          HasUnknownFieldsRecursively( reflection->GetMessage( message, field ) ) )
                {
                    return true;
                }
            }
            return false;
        }

        bool VoterIdBytewiseLess( const ConsensusManager::Vote &lhs, const ConsensusManager::Vote &rhs )
        {
            return std::lexicographical_compare(
                lhs.voter_id().begin(),
                lhs.voter_id().end(),
                rhs.voter_id().begin(),
                rhs.voter_id().end(),
                []( char a, char b )
                { return static_cast<unsigned char>( a ) < static_cast<unsigned char>( b ); } );
        }

        outcome::result<std::string> SerializeCertificateDeterministically(
            const ConsensusManager::Certificate &certificate )
        {
            {
                std::string serialized( certificate.ByteSizeLong(), '\0' );
                google::protobuf::io::ArrayOutputStream output( serialized.data(),
                                                                static_cast<int>( serialized.size() ) );
                google::protobuf::io::CodedOutputStream coded_output( &output );
                coded_output.SetSerializationDeterministic( true );
                if ( !certificate.SerializeToCodedStream( &coded_output ) || coded_output.HadError() )
                {
                    return outcome::failure( ConsensusManager::CertificateStoreError::InvalidCertificate );
                }
                return serialized;
            }
        }

        bool IsPublicChainMintChainId( std::string_view chain_id )
        {
            if ( chain_id.empty() )
            {
                return false;
            }
            if ( chain_id == "public" )
            {
                return true;
            }
            return std::all_of( chain_id.begin(),
                                chain_id.end(),
                                []( unsigned char c ) { return c >= '0' && c <= '9'; } );
        }

        std::optional<base::Hash256> ParseSubjectTypeHash( const ConsensusSubject &subject )
        {
            if ( !subject.has_subject_type_hash() )
            {
                return std::nullopt;
            }
            auto hash = base::Hash256::fromString( subject.subject_type_hash().hash() );
            return hash.has_value() ? std::optional<base::Hash256>{ hash.value() } : std::nullopt;
        }

        enum class BuiltinSubjectKind
        {
            Nonce,
            TaskResult,
            RegistryBatch,
            Other,
        };

        BuiltinSubjectKind GetBuiltinSubjectKind( const ConsensusSubject &subject )
        {
            const auto actual = ParseSubjectTypeHash( subject );
            if ( !actual )
            {
                return BuiltinSubjectKind::Other;
            }

            static const auto nonce          = crypto::sha2_256( NONCE_SUBJECT_TYPE.data(), NONCE_SUBJECT_TYPE.size() );
            static const auto task_result    = crypto::sha2_256( TASK_RESULT_SUBJECT_TYPE.data(),
                                                                 TASK_RESULT_SUBJECT_TYPE.size() );
            static const auto registry_batch = crypto::sha2_256( REGISTRY_BATCH_SUBJECT_TYPE.data(),
                                                                 REGISTRY_BATCH_SUBJECT_TYPE.size() );

            if ( actual.value() == nonce )
            {
                return BuiltinSubjectKind::Nonce;
            }
            if ( actual.value() == task_result )
            {
                return BuiltinSubjectKind::TaskResult;
            }
            if ( actual.value() == registry_batch )
            {
                return BuiltinSubjectKind::RegistryBatch;
            }
            return BuiltinSubjectKind::Other;
        }

        outcome::result<ConsensusStateStore::BurnOutpoint> DecodeMintBurnOutpoint(
            const ConsensusManager::Subject &subject )
        {
            BOOST_OUTCOME_TRY( auto nonce, ConsensusManager::DecodeNonceSubject( subject ) );
            if ( !nonce.transaction().has_mint_v2() ||
                 nonce.transaction().mint_v2().utxo_params().inputs_size() != 1 )
            {
                return outcome::failure( std::errc::invalid_argument );
            }
            const auto &mint = nonce.transaction().mint_v2();
            const auto &input = mint.utxo_params().inputs( 0 );
            ConsensusStateStore::BurnOutpoint outpoint{ mint.chain_id(), input.tx_id_hash(), input.output_index() };
            const bool canonical_chain = !outpoint.source_chain.empty() &&
                ( outpoint.source_chain == "0" || outpoint.source_chain.front() != '0' ) &&
                std::all_of( outpoint.source_chain.begin(), outpoint.source_chain.end(),
                             []( unsigned char c ) { return c >= '0' && c <= '9'; } );
            if ( !canonical_chain || !IsCanonicalHash( outpoint.burn_hash ) ||
                 outpoint.burn_hash == std::string( 64, '0' ) )
            {
                return outcome::failure( std::errc::invalid_argument );
            }
            return outpoint;
        }

        outcome::result<std::string> MintSlotForOutpoint(
            const ConsensusStateStore::BurnOutpoint &outpoint )
        {
            return GeniusTransaction::HashSlotPreimage(
                "mint-v2:" + outpoint.source_chain + ":" + outpoint.burn_hash + ":" +
                std::to_string( outpoint.receipt_log_index ) );
        }

        outcome::result<std::string> ResolveBuiltinNonceSlot( const ConsensusManager::Subject &subject )
        {
            BOOST_OUTCOME_TRY( auto nonce, ConsensusManager::DecodeNonceSubject( subject ) );
            if ( nonce.transaction().has_mint_v2() )
            {
                BOOST_OUTCOME_TRY( auto outpoint, DecodeMintBurnOutpoint( subject ) );
                return MintSlotForOutpoint( outpoint );
            }
            BOOST_OUTCOME_TRY( auto preimage,
                               GeniusTransaction::MakeNonceSlotPreimage( subject.account_id(), nonce.nonce() ) );
            return GeniusTransaction::HashSlotPreimage( preimage );
        }
    } // namespace

    base::Logger ConsensusManagerLogger()
    {
        // Always call base::createLogger to get the current logger
        // This will return existing logger or create new one as needed
        return base::createLogger( "ConsensusManager" );
    }

    std::shared_ptr<ConsensusManager> ConsensusManager::New( std::shared_ptr<ValidatorRegistry>         registry,
                                                             std::shared_ptr<crdt::GlobalDB>            db,
                                                             std::shared_ptr<ipfs_pubsub::GossipPubSub> pubsub,
                                                             Signer                                     signer,
                                                             std::string                                address,
                                                             std::string                                consensus_topic,
                                                             ConsensusConfig                            config )
    {
        if ( !registry )
        {
            ConsensusManagerLogger()->error( "{}: Failed to create ConsensusManager: registry is null", __func__ );
            return nullptr;
        }
        if ( !db )
        {
            ConsensusManagerLogger()->error( "{}: Failed to create ConsensusManager: db is null", __func__ );
            return nullptr;
        }
        if ( !pubsub )
        {
            ConsensusManagerLogger()->error( "{}: Failed to create ConsensusManager: pubsub is null", __func__ );
            return nullptr;
        }
        if ( !signer )
        {
            ConsensusManagerLogger()->error( "{}: Failed to create ConsensusManager: signer is null", __func__ );
            return nullptr;
        }
        if ( address.empty() )
        {
            ConsensusManagerLogger()->error( "{}: Failed to create ConsensusManager: address is empty", __func__ );
            return nullptr;
        }

        if ( !EnsureBuiltinSlotKeyHandlers() )
        {
            ConsensusManagerLogger()->error( "{}: Failed to install built-in slot key handlers", __func__ );
            return nullptr;
        }

        auto instance = std::shared_ptr<ConsensusManager>( new ConsensusManager( std::move( registry ),
                                                                                 std::move( db ),
                                                                                 std::move( pubsub ),
                                                                                 std::move( signer ),
                                                                                 std::move( address ),
                                                                                 std::move( consensus_topic ),
                                                                                 config ) );
        instance->certificate_work_journal_ = instance->db_->GetWorkJournal();

        if ( !instance->certificate_work_journal_ )
        {
            ConsensusManagerLogger()->error( "{}: Failed to create ConsensusManager: crdt work journal is empty",
                                             __func__ );
            return nullptr;
        }

        if ( !instance->RestoreLocalState() )
        {
            ConsensusManagerLogger()->critical(
                "{}: refusing consensus startup because durable consensus state failed strict restoration",
                __func__ );
            return nullptr;
        }

        instance->EmitStartupEvent( "subscribe" );
        instance->consensus_subs_future_ = instance->pubsub_->Subscribe(
            instance->consensus_messages_topic_,
            [weakptr( std::weak_ptr<ConsensusManager>( instance ) )](
                boost::optional<const ipfs_pubsub::GossipPubSub::Message &> message )
            {
                if ( auto self = weakptr.lock() )
                {
                    auto activity = self->BeginActivity();
                    if ( !activity ) return;
                    ConsensusManagerLogger()->trace( "{}: Received Consensus Message on topic {}",
                                                     __func__,
                                                     self->consensus_messages_topic_ );
                    self->OnConsensusMessage( message );
                }
            } );
        ConsensusManagerLogger()->debug( "{}: Subscribed to Consensus topic {}",
                                         __func__,
                                         instance->consensus_messages_topic_ );
        instance->EmitStartupEvent( "certificate-filter" );
        if ( !instance->RegisterCertificateFilter() )
        {
            ConsensusManagerLogger()->error( "{}: Failed to register certificate filter", __func__ );
            return nullptr;
        }
        instance->EmitStartupEvent( "timer" );
        instance->StartRoundTimer();
        instance->RecoverPendingCertificateWork();
        instance->RecoverRestoredCertificateWork();
        instance->ReplayRestoredVotes();

        return instance;
    }

    ConsensusManager::ConsensusManager( std::shared_ptr<ValidatorRegistry>         registry,
                                        std::shared_ptr<crdt::GlobalDB>            db,
                                        std::shared_ptr<ipfs_pubsub::GossipPubSub> pubsub,
                                        Signer                                     signer,
                                        std::string                                address,
                                        std::string                                consensus_topic,
                                        ConsensusConfig                            config ) :
        registry_( std::move( registry ) ), //
        db_( std::move( db ) ),             //
        signer_( std::move( signer ) ),     //
        account_address_( std::move( address ) ), //
        pubsub_( std::move( pubsub ) ),     //
        config_( config.vote_selection_window ), //
        consensus_messages_topic_( fmt::format( "{}{}{}",
                                                CONSENSUS_CHANNEL_PREFIX,
                                                sgns::version::GetNetAndVersionAppendix(),
                                                consensus_topic ) ),
        consensus_datastore_topic_( consensus_messages_topic_ + "#datastore" )
    {
        certificate_record_reader_ = [this]( const crdt::HierarchicalKey &key ) { return db_->Get( key ); };
    }

    uint64_t ConsensusManager::CurrentTimeMs()
    {
        if ( system_now_override_ ) return system_now_override_();
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );
    }

    void ConsensusManager::EmitStartupEvent( std::string_view event ) const
    {
        if ( startup_event_observer_ ) startup_event_observer_( event );
    }

    bool ConsensusManager::RestoreLocalState()
    {
        auto datastore = db_->GetDataStore();
        if ( !datastore || !HasCompatibleCertificateState() ) return false;

        state_store_ = std::make_shared<ConsensusStateStore>( std::move( datastore ) );
        if ( startup_local_query_override_ ) state_store_->query_ = startup_local_query_override_;
        auto votes = state_store_->ScanVotes();
        auto processes = state_store_->ScanProcesses();
        auto conflicts = state_store_->ScanConflicts();
        auto safeties = state_store_->ScanSafety();
        auto reservations = state_store_->ScanBurnReservations();
        if ( !votes || !processes || !conflicts || !safeties || !reservations ) return false;
        certificate_conflict_unique_pairs_.store( conflicts.value().size(), std::memory_order_relaxed );
        for ( const auto &record : conflicts.value() )
        {
            restored_safety_proposal_ids_.insert( record.authoritative_proposal_id() );
            restored_safety_proposal_ids_.insert( record.incoming_proposal_id() );
        }

        struct FinalRecord
        {
            std::string digest;
            std::string proposal_id;
            std::string winner_id;
            std::optional<ConsensusStateStore::BurnOutpoint> burn_outpoint;
        };
        std::unordered_map<std::string, FinalRecord> finals;
        auto certificate_entries = db_->QueryKeyValues( CERTIFICATE_SLOT_BASE_PATH_KEY );
        if ( certificate_entries.has_error() ) return false;
        for ( const auto &[stored_key, stored_value] : certificate_entries.value() )
        {
            auto key = db_->KeyToString( stored_key );
            if ( key.has_error() ) return false;
            const auto marker = key.value().find( std::string( CERTIFICATE_SLOT_BASE_PATH_KEY ) );
            if ( marker == std::string::npos ) return false;
            const auto slot = key.value().substr( marker + CERTIFICATE_SLOT_BASE_PATH_KEY.size() );
            if ( slot.size() != 64 ) return false;

            Certificate certificate;
            const auto bytes = stored_value.toString();
            if ( !certificate.ParseFromArray( stored_value.data(), stored_value.size() ) ) return false;
            auto normalized = NormalizeCertificateStructural( certificate );
            if ( normalized.check != Check::Approve || normalized.deterministic_bytes != bytes ) return false;
            auto actual_slot = GetSlotKey( normalized.certificate.proposal() );
            auto winner = GetSubjectHash( normalized.certificate.proposal().subject() );
            if ( !actual_slot || actual_slot.value() != slot || !winner ) return false;
            auto digest_bytes = crypto::sha2_256( bytes.data(), bytes.size() );
            const auto digest = base::hex_lower(
                gsl::span<const uint8_t>( digest_bytes.data(), digest_bytes.size() ) );
            std::optional<ConsensusStateStore::BurnOutpoint> burn_outpoint;
            if ( normalized.certificate.proposal().subject().has_subject_type_hash() &&
                 SubjectTypeMatches( normalized.certificate.proposal().subject(), NONCE_SUBJECT_TYPE ) )
            {
                auto decoded = DecodeMintBurnOutpoint( normalized.certificate.proposal().subject() );
                if ( decoded.has_value() ) burn_outpoint = decoded.value();
            }
            if ( !finals.emplace( slot,
                                  FinalRecord{ digest, normalized.certificate.proposal_id(), winner.value(),
                                               std::move( burn_outpoint ) } )
                      .second )
                return false;
        }

        restored_votes_ = std::move( votes.value() );
        for ( const auto &record : restored_votes_ )
        {
            if ( record.validator_id() != account_address_ ) return false;
            Vote vote;
            Proposal proposal;
            if ( !vote.ParseFromString( record.signed_vote_bytes() ) ||
                 !proposal.ParseFromString( record.signed_proposal_bytes() ) || !CheckProposal( proposal ) ||
                 !ValidateSubject( proposal.subject() ) )
                return false;
            auto vote_bytes = VoteSigningBytes( vote );
            auto slot = GetSlotKey( proposal );
            if ( !vote_bytes || !GeniusAccount::VerifySignature( vote.voter_id(), vote.signature(), vote_bytes.value() ) ||
                 !slot || slot.value() != record.slot_id() )
                return false;
        }

        const auto now = CurrentTimeMs();

        std::unordered_map<std::string, ConsensusStateStore::BurnReservationRecord> restored_reservations;
        for ( auto record : reservations.value() )
        {
            const ConsensusStateStore::BurnOutpoint outpoint{
                record.source_chain(), record.burn_hash(), record.receipt_log_index()
            };
            auto canonical_slot = MintSlotForOutpoint( outpoint );
            if ( !canonical_slot || canonical_slot.value() != record.slot_id() ) return false;

            auto final = finals.find( record.slot_id() );
            if ( final == finals.end() )
            {
                if ( record.state() != ConsensusStateStore::BurnReservationRecord::RESERVED ) return false;
            }
            else
            {
                if ( !final->second.burn_outpoint.has_value() ||
                     final->second.burn_outpoint->source_chain != outpoint.source_chain ||
                     final->second.burn_outpoint->burn_hash != outpoint.burn_hash ||
                     final->second.burn_outpoint->receipt_log_index != outpoint.receipt_log_index )
                    return false;
                if ( record.state() == ConsensusStateStore::BurnReservationRecord::RESERVED )
                {
                    auto finalized = state_store_->FinalizeBurnReservation(
                        record.slot_id(), outpoint, final->second.digest, final->second.proposal_id,
                        final->second.winner_id, now );
                    if ( !finalized ) return false;
                    record = std::move( finalized.value() );
                }
                else if ( record.certificate_digest() != final->second.digest ||
                          record.proposal_id() != final->second.proposal_id ||
                          record.winner_id() != final->second.winner_id )
                    return false;
            }
            if ( record.state() == ConsensusStateStore::BurnReservationRecord::SAFETY_ERROR ||
                 record.state() == ConsensusStateStore::BurnReservationRecord::CONSUMED_SAFETY_ERROR )
                restored_safety_slots_.insert( record.slot_id() );
            if ( !restored_reservations.emplace( record.slot_id(), std::move( record ) ).second ) return false;
        }

        // A mint certificate is authoritative even when this node never saw a
        // candidate: synthesize durable final-pending protection before handler recovery.
        for ( const auto &[slot, final] : finals )
        {
            if ( !final.burn_outpoint.has_value() || restored_reservations.count( slot ) != 0 ) continue;
            auto finalized = state_store_->FinalizeBurnReservation(
                slot, *final.burn_outpoint, final.digest, final.proposal_id, final.winner_id, now );
            if ( !finalized ) return false;
            restored_reservations.emplace( slot, std::move( finalized.value() ) );
        }

        // Durable votes need no candidate object after restart.  Their exact
        // acceptance horizon extends Reserved protection, including equality.
        for ( const auto &vote : restored_votes_ )
        {
            auto reservation = restored_reservations.find( vote.slot_id() );
            if ( reservation == restored_reservations.end() ||
                 reservation->second.state() != ConsensusStateStore::BurnReservationRecord::RESERVED )
                continue;
            const ConsensusStateStore::BurnOutpoint outpoint{
                reservation->second.source_chain(), reservation->second.burn_hash(),
                reservation->second.receipt_log_index()
            };
            if ( vote.acceptance_horizon_ms() > reservation->second.candidate_acceptance_horizon_ms() )
            {
                auto extended = state_store_->CreateOrJoinBurnReservation(
                    vote.slot_id(), outpoint, vote.acceptance_horizon_ms(), now );
                if ( !extended ) return false;
                reservation->second = std::move( extended.value().record );
            }
        }

        for ( auto record : processes.value() )
        {
            auto final = finals.find( record.slot_id() );
            if ( final == finals.end() || final->second.digest != record.certificate_digest() ||
                 final->second.proposal_id != record.proposal_id() || final->second.winner_id != record.winner_id() )
                return false;
            if ( record.state() == ConsensusStateStore::ProcessRecord::PROCESSING )
            {
                if ( !state_store_->RestorePending( record.slot_id(), now ) ) return false;
                record.set_state( ConsensusStateStore::ProcessRecord::PENDING );
                record.set_lease_until_ms( 0 );
                record.set_updated_at_ms( now );
            }
            restored_processes_.emplace( record.slot_id(), std::move( record ) );
        }

        for ( const auto &[slot, final] : finals )
        {
            restored_final_slots_.insert( slot );
            if ( restored_processes_.find( slot ) == restored_processes_.end() )
            {
                ConsensusStateStore::ProcessRecord pending;
                pending.set_schema_version( 2 );
                pending.set_state( ConsensusStateStore::ProcessRecord::PENDING );
                pending.set_slot_id( slot );
                pending.set_certificate_digest( final.digest );
                pending.set_proposal_id( final.proposal_id );
                pending.set_winner_id( final.winner_id );
                pending.set_updated_at_ms( now );
                if ( !state_store_->PutPendingProcess( pending ) ) return false;
                restored_processes_.emplace( slot, std::move( pending ) );
            }
            auto &slot_state = slot_states_[slot];
            slot_state.reserved_finalization_proposal_id = final.proposal_id;
            slot_state.reserved_finalization_digest = final.digest;
            slot_state.reserved_finalization_winner_id = final.winner_id;
            if ( restored_safety_slots_.count( slot ) != 0 )
                slot_state.lifecycle = SlotState::Lifecycle::SafetyViolation;
            else
                slot_state.lifecycle = restored_processes_.at( slot ).state() ==
                                               ConsensusStateStore::ProcessRecord::COMPLETE
                                           ? SlotState::Lifecycle::Applied
                                           : SlotState::Lifecycle::FinalizedPendingApplication;
        }

        for ( const auto &record : safeties.value() )
        {
            auto final = finals.find( record.slot_id() );
            if ( final == finals.end() || final->second.digest != record.authoritative_certificate_digest() ||
                 final->second.proposal_id != record.authoritative_proposal_id() )
                return false;
            restored_safety_slots_.insert( record.slot_id() );
            slot_states_[record.slot_id()].lifecycle = SlotState::Lifecycle::SafetyViolation;
        }
        for ( const auto &record : restored_votes_ )
        {
            auto &slot = slot_states_[record.slot_id()];
            slot.generation = record.generation();
            slot.durable_generation = record.generation();
            slot.durable_proposal_id = record.proposal_id();
            slot.frozen_proposal_id = record.proposal_id();
            slot.publication_count = record.publication_count();
            slot.last_publication_at_ms = record.last_publication_at_ms();
            slot.last_publication_succeeded = record.last_publication_succeeded();
            if ( restored_safety_slots_.count( record.slot_id() ) != 0 )
                slot.lifecycle = SlotState::Lifecycle::SafetyViolation;
            else if ( restored_final_slots_.count( record.slot_id() ) != 0 )
                slot.lifecycle = restored_processes_.at( record.slot_id() ).state() ==
                                         ConsensusStateStore::ProcessRecord::COMPLETE
                                     ? SlotState::Lifecycle::Applied
                                     : SlotState::Lifecycle::FinalizedPendingApplication;
            else if ( record.state() == ConsensusStateStore::VoteRecord::RETIRED )
                slot.lifecycle = SlotState::Lifecycle::Retired;
            else if ( now > record.acceptance_horizon_ms() )
            {
                if ( !state_store_->RetireVote( record.validator_id(), record.slot_id(), now ) ) return false;
                slot.lifecycle = SlotState::Lifecycle::Retired;
            }
            else
                slot.lifecycle = SlotState::Lifecycle::Voted;
        }
        for ( const auto &record : restored_votes_ )
        {
            auto &slot = slot_states_[record.slot_id()];
            slot.generation = record.generation();
            slot.durable_generation = record.generation();
            slot.durable_proposal_id = record.proposal_id();
            slot.frozen_proposal_id = record.proposal_id();
            slot.publication_count = record.publication_count();
            slot.last_publication_at_ms = record.last_publication_at_ms();
            slot.last_publication_succeeded = record.last_publication_succeeded();
            if ( restored_safety_slots_.count( record.slot_id() ) != 0 )
                slot.lifecycle = SlotState::Lifecycle::SafetyViolation;
            else if ( restored_final_slots_.count( record.slot_id() ) != 0 )
                slot.lifecycle = SlotState::Lifecycle::FinalizedPendingApplication;
            else if ( record.state() == ConsensusStateStore::VoteRecord::RETIRED )
                slot.lifecycle = SlotState::Lifecycle::Retired;
            else if ( now > record.acceptance_horizon_ms() )
            {
                if ( !state_store_->RetireVote( record.validator_id(), record.slot_id(), now ) ) return false;
                slot.lifecycle = SlotState::Lifecycle::Retired;
            }
            else
                slot.lifecycle = SlotState::Lifecycle::Voted;
        }
        EmitStartupEvent( "restored" );
        return true;
    }

    ConsensusManager::CertificateStoreError ConsensusManager::MapCertificateReadError(
        const std::error_code &error )
    {
        if ( error == make_error_code( storage::DatabaseError::NOT_FOUND ) )
        {
            return CertificateStoreError::NotFound;
        }
        if ( error == make_error_code( storage::DatabaseError::CORRUPTION ) )
        {
            return CertificateStoreError::IntegrityError;
        }
        return CertificateStoreError::StorageError;
    }

    outcome::result<std::optional<crdt::GlobalDB::Buffer>> ConsensusManager::ReadCertificatePreflightRecord(
        const crdt::HierarchicalKey &key ) const
    {
        auto result = certificate_record_reader_( key );
        if ( result.has_value() )
        {
            return std::optional<crdt::GlobalDB::Buffer>{ std::move( result.value() ) };
        }

        const auto mapped = MapCertificateReadError( result.error() );
        if ( mapped == CertificateStoreError::NotFound )
        {
            return std::optional<crdt::GlobalDB::Buffer>{};
        }

        ConsensusManagerLogger()->critical(
            "{}: certificate preflight read failed key={} raw_error={} mapped_error={}",
            __func__,
            key.GetKey(),
            result.error().message(),
            make_error_code( mapped ).message() );
        return outcome::failure( mapped );
    }

    ConsensusManager::~ConsensusManager()
    {
        Close();
        ConsensusManagerLogger()->debug( "{}: Finished shutting down ConsensusManager", __func__ );
    }

    void ConsensusManager::Close()
    {
        std::unique_lock close_lock( close_mutex_ );
        if ( close_complete_ ) return;
        {
            std::lock_guard activity_lock( activity_state_->mutex );
            activity_state_->closing = true;
            activity_state_->cv.notify_all();
        }
        stop_timer_.store( true );
        timer_cv_.notify_all();
        slot_cv_.notify_all();
        if ( db_ )
        {
            // Account switches reuse GlobalDB. Remove only registrations owned
            // by this manager so a delayed close cannot remove replacements.
            if ( certificate_callback_registered_ )
            {
                db_->UnregisterNewElementCallback( std::string( CERT_SLOT_KEY_PATTERN ) );
                certificate_callback_registered_ = false;
            }
            if ( certificate_filter_registered_ )
            {
                db_->UnregisterElementFilter( std::string( CERT_SLOT_KEY_PATTERN ) );
                certificate_filter_registered_ = false;
            }
            if ( certificate_delta_filter_registered_ )
            {
                db_->UnregisterDeltaFilter( std::string( CERT_NAMESPACE_KEY_PATTERN ) );
                certificate_delta_filter_registered_ = false;
            }
        }
        if ( round_timer_.joinable() )
        {
            round_timer_.join();
        }
        {
            std::unique_lock activity_lock( activity_state_->mutex );
            activity_state_->cv.wait( activity_lock, [this]() { return activity_state_->active == 0; } );
        }
        close_complete_ = true;
    }

    std::shared_ptr<void> ConsensusManager::BeginActivity()
    {
        auto state = activity_state_;
        {
            std::lock_guard lock( state->mutex );
            if ( state->closing ) return {};
            ++state->active;
        }
        return std::shared_ptr<void>(
            state.get(),
            [state]( void * )
            {
                std::lock_guard lock( state->mutex );
                if ( --state->active == 0 ) state->cv.notify_all();
            } );
    }

    void ConsensusManager::StartRoundTimer()
    {
        if ( round_timer_.joinable() )
        {
            return;
        }
        if ( stop_timer_.load() )
        {
            return;
        }

        auto *self           = this;
        auto  activity_state = activity_state_;
        round_timer_         = std::thread(
            [self, activity_state]()
            {
                constexpr auto min_interval = std::chrono::milliseconds( 500 );
                while ( true )
                {
                    {
                        std::lock_guard lock( activity_state->mutex );
                        if ( activity_state->closing ) return;
                        ++activity_state->active;
                    }
                    auto activity = std::shared_ptr<void>(
                        activity_state.get(),
                        [activity_state]( void * )
                        {
                            std::lock_guard lock( activity_state->mutex );
                            if ( --activity_state->active == 0 ) activity_state->cv.notify_all();
                        } );

                    std::unique_lock<std::mutex> lock( self->timer_mutex_ );
                    auto                         interval = self->round_duration_ / 2;
                    if ( interval.count() <= 0 )
                    {
                        interval = DEFAULT_ROUND_DURATION / 2;
                    }
                    if ( interval < min_interval )
                    {
                        interval = min_interval;
                    }
                    {
                        std::lock_guard slot_lock( self->proposals_mutex_ );
                        const auto now = self->steady_now_override_ ? self->steady_now_override_()
                                                                    : std::chrono::steady_clock::now();
                        for ( const auto &[unused_slot, state] : self->slot_states_ )
                        {
                            (void) unused_slot;
                            if ( state.lifecycle == SlotState::Lifecycle::Selecting && state.deadline > now )
                            {
                                interval = std::min(
                                    interval,
                                    std::chrono::duration_cast<std::chrono::milliseconds>( state.deadline - now ) );
                            }
                            else if ( state.lifecycle == SlotState::Lifecycle::Selecting )
                            {
                                interval = std::chrono::milliseconds( 0 );
                            }
                        }
                    }
                    if ( self->certificates_pending_.load() )
                    {
                        // Work is pending: run on cadence, only interrupt for shutdown.
                        self->timer_cv_.wait_for( lock, interval, [self]() { return self->stop_timer_.load(); } );
                    }
                    else
                    {
                        // No pending work: wait up to interval, but wake immediately when new work appears.
                        self->timer_cv_.wait_for(
                            lock,
                            interval,
                            [self]() { return self->stop_timer_.load() || self->certificates_pending_.load(); } );
                    }
                    if ( self->stop_timer_.load() )
                    {
                        return;
                    }
                    lock.unlock();
                    const auto steady_now = self->steady_now_override_ ? self->steady_now_override_()
                                                                        : std::chrono::steady_clock::now();
                    self->ProcessCandidateDeadlines( steady_now );
                    if ( !self->suppress_timer_burn_reconciliation_ ) self->ReconcileBurnReservations();
                    if ( self->certificates_pending_.load() )
                    {
                        self->ProcessCertificates();
                        self->UpdateCertificatesPending();
                    }
                    self->ExpirePendingProposals();
                    self->ProcessDuePendingRetries();
                    // Keep replaying unfinished certificate work while the node is running.
                    self->RecoverPendingCertificateWork();
                }
            } );
    }

    outcome::result<void> ConsensusManager::Publish( const ConsensusMessage &message )
    {
        std::string serialized_proto;
        if ( !message.SerializeToString( &serialized_proto ) )
        {
            ConsensusManagerLogger()->error( "{}: Failed to serialize consensus message", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }

        return PublishSerialized( serialized_proto );
    }

    outcome::result<void> ConsensusManager::PublishSerialized( std::string_view envelope_bytes )
    {
        if ( envelope_bytes.empty() ) return outcome::failure( std::errc::invalid_argument );
        EmitStartupEvent( "publish" );
        if ( raw_publish_override_ ) return raw_publish_override_( envelope_bytes );

        ConsensusManagerLogger()->debug( "{}: Sending consensus packet to {}", __func__, consensus_messages_topic_ );
        std::vector<uint8_t> serialized_proto( envelope_bytes.begin(), envelope_bytes.end() );
        pubsub_->Publish( consensus_messages_topic_, serialized_proto );
        ConsensusManagerLogger()->debug( "{}: Consensus packet published (bytes={})",
                                         __func__,
                                         serialized_proto.size() );

        return outcome::success();
    }

    void ConsensusManager::EmitConsensusTrace( ConsensusTraceEvent event ) const noexcept
    {
        if ( !consensus_trace_observer_ ) return;
        try
        {
            consensus_trace_observer_( event );
        }
        catch ( ... )
        {
            ConsensusManagerLogger()->warn( "Consensus trace observer threw; ignoring callback failure" );
        }
    }

    bool ConsensusManager::RegisterSubjectHandler( std::string_view subject_type, SubjectHandler handler )
    {
        auto type_hash = ComputeSubjectTypeHash( subject_type );
        if ( type_hash.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: ignored invalid handler subject_type={}", __func__, subject_type );
            return false;
        }
        ConsensusManagerLogger()->debug( "{}: Registering subject handler subject_type={}", __func__, subject_type );
        std::unique_lock lock( subject_handlers_mutex_ );
        subject_handlers_[type_hash.value()] = std::move( handler );
        return true;
    }

    void ConsensusManager::UnregisterSubjectHandler( std::string_view subject_type )
    {
        ConsensusManagerLogger()->debug( "{}: Removing Subject handler with subject_type={}", __func__, subject_type );
        auto type_hash = ComputeSubjectTypeHash( subject_type );
        if ( type_hash.has_error() )
        {
            return;
        }
        std::unique_lock lock( subject_handlers_mutex_ );
        subject_handlers_.erase( type_hash.value() );
    }

    bool ConsensusManager::RegisterCertificateHandler( std::string_view          subject_type,
                                                       CertificateSubjectHandler handler )
    {
        auto type_hash = ComputeSubjectTypeHash( subject_type );
        if ( type_hash.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: ignored invalid certificate handler subject_type={}",
                                             __func__,
                                             subject_type );
            return false;
        }
        ConsensusManagerLogger()->debug( "{}: Registering certificate handler subject_type={}",
                                         __func__,
                                         subject_type );
        std::unique_lock lock( certificate_handlers_mutex_ );
        certificate_subject_handlers_[type_hash.value()] = std::move( handler );
        lock.unlock();
        RecoverRestoredCertificateWork();
        return true;
    }

    bool ConsensusManager::RegisterCertificateApplicationHandler(
        std::string_view subject_type, CertificateApplicationHandler handler )
    {
        if ( !handler ) return false;
        auto type_hash = ComputeSubjectTypeHash( subject_type );
        if ( type_hash.has_error() ) return false;
        {
            std::unique_lock lock( certificate_handlers_mutex_ );
            if ( !certificate_application_handlers_.try_emplace(
                     type_hash.value(), std::move( handler ) ).second )
                return false;
        }
        RecoverRestoredCertificateWork();
        return true;
    }

    void ConsensusManager::UnregisterCertificateHandler( std::string_view subject_type )
    {
        ConsensusManagerLogger()->debug( "{}: Removing Certificate handler with subject_type={}",
                                         __func__,
                                         subject_type );
        auto type_hash = ComputeSubjectTypeHash( subject_type );
        if ( type_hash.has_error() )
        {
            return;
        }
        std::unique_lock lock( certificate_handlers_mutex_ );
        certificate_subject_handlers_.erase( type_hash.value() );
        certificate_application_handlers_.erase( type_hash.value() );
    }

    bool ConsensusManager::RegisterProposalCleanupHandler( std::string_view       subject_type,
                                                           ProposalCleanupHandler handler )
    {
        auto type_hash = ComputeSubjectTypeHash( subject_type );
        if ( type_hash.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: ignored invalid cleanup handler subject_type={}",
                                             __func__,
                                             subject_type );
            return false;
        }
        ConsensusManagerLogger()->debug( "{}: Registering cleanup handler subject_type={}", __func__, subject_type );
        std::unique_lock lock( cleanup_handlers_mutex_ );
        proposal_cleanup_handlers_[type_hash.value()].push_back( std::move( handler ) );
        return true;
    }

    void ConsensusManager::UnregisterProposalCleanupHandler( std::string_view subject_type )
    {
        ConsensusManagerLogger()->debug( "{}: Removing cleanup handler with subject_type={}", __func__, subject_type );
        auto type_hash = ComputeSubjectTypeHash( subject_type );
        if ( type_hash.has_error() )
        {
            return;
        }
        std::unique_lock lock( cleanup_handlers_mutex_ );
        proposal_cleanup_handlers_.erase( type_hash.value() );
    }

    bool ConsensusManager::RegisterResourceAdmissionHandler( std::string_view subject_type,
                                                              ResourceAdmissionHandler handler )
    {
        if ( !handler ) return false;
        auto type_hash = ComputeSubjectTypeHash( subject_type );
        if ( type_hash.has_error() ) return false;
        std::unique_lock lock( resource_admission_handlers_mutex_ );
        return resource_admission_handlers_.emplace( type_hash.value(), std::move( handler ) ).second;
    }

    void ConsensusManager::UnregisterResourceAdmissionHandler( std::string_view subject_type )
    {
        auto type_hash = ComputeSubjectTypeHash( subject_type );
        if ( type_hash.has_error() ) return;
        std::unique_lock lock( resource_admission_handlers_mutex_ );
        resource_admission_handlers_.erase( type_hash.value() );
    }

    outcome::result<std::optional<ConsensusStateStore::BurnReservationRecord>>
    ConsensusManager::GetBurnReservation( const ConsensusStateStore::BurnOutpoint &outpoint ) const
    {
        return state_store_->GetBurnReservation( outpoint );
    }

    void ConsensusManager::SetPendingLifecycleConfig( PendingLifecycleConfig config )
    {
        std::lock_guard lock( proposals_mutex_ );
        pending_config_ = config;
    }

    bool ConsensusManager::RegisterSlotKeyHandler( std::string_view subject_type, SlotKeyHandler handler )
    {
        if ( !handler )
        {
            ConsensusManagerLogger()->error( "{}: ignored empty slot key handler subject_type={}",
                                             __func__,
                                             subject_type );
            return false;
        }
        auto type_hash = ComputeSubjectTypeHash( subject_type );
        if ( type_hash.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: ignored invalid slot key handler subject_type={}",
                                             __func__,
                                             subject_type );
            return false;
        }
        ConsensusManagerLogger()->debug( "{}: Registering slot key handler subject_type={}", __func__, subject_type );
        std::unique_lock lock( slot_key_handlers_mutex_ );
        return slot_key_handlers_.emplace( type_hash.value(), std::move( handler ) ).second;
    }

    bool ConsensusManager::EnsureBuiltinSlotKeyHandlers()
    {
        auto type_hash = ComputeSubjectTypeHash( NONCE_SUBJECT_TYPE );
        if ( type_hash.has_error() ) return false;
        std::unique_lock lock( slot_key_handlers_mutex_ );
        if ( slot_key_handlers_.find( type_hash.value() ) != slot_key_handlers_.end() ) return true;
        return slot_key_handlers_.emplace( type_hash.value(), ResolveBuiltinNonceSlot ).second;
    }

    void ConsensusManager::UnregisterSlotKeyHandler( std::string_view subject_type )
    {
        ConsensusManagerLogger()->debug( "{}: Removing slot key handler subject_type={}", __func__, subject_type );
        auto type_hash = ComputeSubjectTypeHash( subject_type );
        if ( type_hash.has_error() )
        {
            return;
        }
        std::unique_lock lock( slot_key_handlers_mutex_ );
        slot_key_handlers_.erase( type_hash.value() );
    }

    void ConsensusManager::FireProposalCleanupCallbacks( const Proposal &proposal )
    {
        auto subject_hash = GetSubjectHash( proposal.subject() );
        if ( subject_hash.has_error() )
        {
            return;
        }
        auto nonce_payload = DecodeNonceSubject( proposal.subject() );
        if ( nonce_payload.has_error() )
        {
            return;
        }
        auto tx_hash = nonce_payload.value().tx_hash();
        if ( tx_hash.empty() )
        {
            return;
        }
        auto type_hash = ParseSubjectTypeHash( proposal.subject() );
        if ( !type_hash )
        {
            return;
        }

        std::vector<ProposalCleanupHandler> handlers_copy;
        {
            std::shared_lock lock( cleanup_handlers_mutex_ );
            auto             it = proposal_cleanup_handlers_.find( type_hash.value() );
            if ( it != proposal_cleanup_handlers_.end() )
            {
                handlers_copy = it->second;
            }
        }
        for ( auto &handler : handlers_copy )
        {
            handler( tx_hash );
        }
    }

    void ConsensusManager::ConfigureCertificateDelay( std::chrono::milliseconds delay )
    {
        if ( delay.count() < 0 )
        {
            ConsensusManagerLogger()->warn( "{}: using zero delay", __func__ );
            certificate_delay_ = std::chrono::milliseconds( 0 );
            return;
        }
        certificate_delay_ = delay;
    }

    bool ConsensusManager::IsTimestampSane( uint64_t timestamp_ms ) const
    {
        if ( timestamp_ms == 0 )
        {
            return false;
        }
        const auto now_ms    = static_cast<int64_t>( CurrentTimeMs() );
        const auto window_ms = timestamp_window_.count();
        if ( now_ms < 0 || window_ms < 0 )
        {
            return false;
        }

        const auto now        = static_cast<uint64_t>( now_ms );
        const auto difference = timestamp_ms > now ? timestamp_ms - now : now - timestamp_ms;
        return difference <= static_cast<uint64_t>( timestamp_window_.count() );
    }

    uint64_t ConsensusManager::GetCurrentRound( uint64_t proposal_ts_ms ) const
    {
        const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch() )
                                .count();
        if ( proposal_ts_ms == 0 || now_ms <= 0 )
        {
            return 0;
        }

        const auto now = static_cast<uint64_t>( now_ms );
        if ( proposal_ts_ms >= now )
        {
            return 0;
        }

        const auto elapsed = now - proposal_ts_ms;
        const auto skew    = static_cast<uint64_t>( round_skew_.count() );
        return elapsed > skew ? ( elapsed - skew ) / static_cast<uint64_t>( round_duration_.count() ) : 0;
    }

    ConsensusManager::AggregatorRole ConsensusManager::GetAggregatorRole(
        const Proposal                    &proposal,
        const ValidatorRegistry::Registry &registry ) const
    {
        ConsensusManagerLogger()->trace( "{}: Checking local aggregator role for proposal", __func__ );

        // We do it ordered so that it is deterministic
        std::vector<std::string> ordered;
        ordered.reserve( registry.validators_size() );
        for ( const auto &entry : registry.validators() )
        {
            if ( entry.status() == ValidatorRegistry::Status::ACTIVE )
            {
                ordered.push_back( entry.validator_id() );
            }
        }
        std::sort( ordered.begin(), ordered.end() );

        if ( ordered.empty() )
        {
            return AggregatorRole::NotInRegistry;
        }

        if ( std::find( ordered.begin(), ordered.end(), account_address_ ) == ordered.end() )
        {
            return AggregatorRole::NotInRegistry;
        }

        const auto proposal_hash = sgns::crypto::sha2_256( proposal.proposal_id().data(),
                                                           proposal.proposal_id().size() );
        uint64_t   proposal_seed = 0;
        for ( size_t i = 0; i < sizeof( proposal_seed ); ++i )
        {
            proposal_seed = ( proposal_seed << 8 ) | proposal_hash[i];
        }

        const auto validator_count = ordered.size();
        const auto starting_index  = proposal_seed % validator_count;
        const auto round_offset    = GetCurrentRound( proposal.timestamp() ) % validator_count;
        const auto selected_index  = ( starting_index + round_offset ) % validator_count;

        return ordered[selected_index] == account_address_ ? AggregatorRole::CurrentAggregator
                                                           : AggregatorRole::ActiveButNotAggregator;
    }

    outcome::result<std::string> ConsensusManager::GetSubjectHash( const Subject &subject )
    {
        switch ( GetBuiltinSubjectKind( subject ) )
        {
            case BuiltinSubjectKind::Nonce:
            {
                BOOST_OUTCOME_TRY( auto payload, DecodeNonceSubject( subject ) );
                if ( payload.tx_hash().empty() )
                {
                    return outcome::failure( std::errc::invalid_argument );
                }
                return payload.tx_hash();
            }
            case BuiltinSubjectKind::TaskResult:
            {
                BOOST_OUTCOME_TRY( auto payload, DecodeTaskResultSubject( subject ) );
                if ( payload.task_result_hash().empty() )
                {
                    return outcome::failure( std::errc::invalid_argument );
                }
                return payload.task_result_hash();
            }
            case BuiltinSubjectKind::RegistryBatch:
            {
                BOOST_OUTCOME_TRY( auto payload, DecodeRegistryBatchSubject( subject ) );
                if ( payload.batch_root().empty() )
                {
                    return outcome::failure( std::errc::invalid_argument );
                }
                return std::string( payload.batch_root() );
            }
            case BuiltinSubjectKind::Other:
                return ComputeSubjectId( subject );
        }
        return outcome::failure( std::errc::invalid_argument );
    }

    void ConsensusManager::ContinueProposalAfterSubject( const Proposal &proposal, const std::string &slot_key )
    {
        const auto &proposal_id = proposal.proposal_id();
        ConsensusManagerLogger()->debug( "{}: Continuing proposal: hash {}, id {}",
                                         __func__,
                                         GetPrintableSubjectHash( proposal.subject() ),
                                         proposal.proposal_id().substr( 0, 8 ) );
        uint64_t retire_generation = 0;
        {
            std::lock_guard lock( proposals_mutex_ );
            auto slot = slot_states_.find( slot_key );
            if ( slot != slot_states_.end() && slot->second.lifecycle == SlotState::Lifecycle::Voted )
                retire_generation = slot->second.durable_generation;
        }
        if ( retire_generation != 0 )
        {
            auto durable = state_store_->GetVote( account_address_, slot_key );
            const auto now = CurrentTimeMs();
            if ( durable && durable.value() && durable.value()->state() == ConsensusStateStore::VoteRecord::ACTIVE &&
                 durable.value()->generation() == retire_generation && now > durable.value()->acceptance_horizon_ms() &&
                 state_store_->RetireVote( account_address_, slot_key, now ) )
            {
                std::lock_guard lock( proposals_mutex_ );
                auto slot = slot_states_.find( slot_key );
                if ( slot != slot_states_.end() && slot->second.lifecycle == SlotState::Lifecycle::Voted &&
                     slot->second.durable_generation == retire_generation )
                    slot->second.lifecycle = SlotState::Lifecycle::Retired;
            }
        }

        bool     replay = false;
        uint64_t replay_generation = 0;

        ConsensusManagerLogger()->debug( "{}: Slot key acquired: hash {}, id {}, slot key {}",
                                         __func__,
                                         GetPrintableSubjectHash( proposal.subject() ),
                                         proposal.proposal_id().substr( 0, 8 ),
                                         slot_key );
        {
            std::lock_guard lock( proposals_mutex_ );
            auto [proposal_it, inserted] = proposals_.try_emplace( proposal_id );
            if ( inserted )
            {
                proposal_it->second.proposal = proposal;
                proposal_it->second.slot_key = slot_key;
            }

            auto &slot_state = slot_states_[slot_key];
            if ( restored_final_slots_.count( slot_key ) != 0 )
            {
                slot_state.lifecycle = SlotState::Lifecycle::FinalizedPendingApplication;
                resource_admissions_inflight_.erase( slot_key );
                slot_cv_.notify_all();
                return;
            }
            if ( restored_safety_slots_.count( slot_key ) != 0 )
            {
                slot_state.lifecycle = SlotState::Lifecycle::SafetyViolation;
                resource_admissions_inflight_.erase( slot_key );
                slot_cv_.notify_all();
                return;
            }
            if ( slot_state.lifecycle == SlotState::Lifecycle::Empty ||
                 slot_state.lifecycle == SlotState::Lifecycle::Retired )
            {
                ++slot_state.generation;
                slot_state.lifecycle = SlotState::Lifecycle::Selecting;
                slot_state.best_proposal_id = proposal.proposal_id();
                const auto steady_now = steady_now_override_ ? steady_now_override_()
                                                              : std::chrono::steady_clock::now();
                slot_state.deadline = steady_now + config_.vote_selection_window;
                auto nonce_payload          = DecodeNonceSubject( proposal.subject() );
                if ( nonce_payload.has_value() )
                {
                    slot_state.best_tx_hash = nonce_payload.value().tx_hash();
                }
                timer_cv_.notify_all();
            }
            else if ( slot_state.lifecycle == SlotState::Lifecycle::Selecting )
            {
                const auto &current = proposals_.at( slot_state.best_proposal_id ).proposal;
                ConsensusManagerLogger()->debug(
                    "{}: Already have a best proposal for hash {}, id={}, slot key {}. Seeing if {} is better ",
                    __func__,
                    GetPrintableSubjectHash( current.subject() ),
                    current.proposal_id().substr( 0, 8 ),
                    slot_key,
                    proposal.proposal_id().substr( 0, 8 ) );
                if ( IsBetterProposal( proposal, current ) )
                {
                    ConsensusManagerLogger()->debug( "{}: Better proposal for hash {}, id={}, slot key {}. ",
                                                     __func__,
                                                     GetPrintableSubjectHash( proposal.subject() ),
                                                     proposal.proposal_id().substr( 0, 8 ),
                                                     slot_key );
                    slot_state.best_proposal_id = proposal.proposal_id();
                    auto nonce_payload          = DecodeNonceSubject( proposal.subject() );
                    if ( nonce_payload.has_value() )
                    {
                        slot_state.best_tx_hash = nonce_payload.value().tx_hash();
                    }
                }
            }
            else if ( slot_state.lifecycle == SlotState::Lifecycle::Voted &&
                      slot_state.durable_proposal_id == proposal.proposal_id() )
            {
                replay = true;
                replay_generation = slot_state.durable_generation;
            }
            else
            {
                slot_state.late_candidate_ids.push_back( proposal.proposal_id() );
            }
            resource_admissions_inflight_.erase( slot_key );
            slot_cv_.notify_all();
        }

        std::vector<Vote> pending_votes;
        {
            std::lock_guard lock( proposals_mutex_ );
            auto            it = pending_votes_.find( proposal_id );
            if ( it != pending_votes_.end() )
            {
                pending_votes = std::move( it->second );
                pending_votes_.erase( it );
            }
        }
        for ( const auto &vote : pending_votes )
        {
            HandleVote( vote );
        }

        if ( replay ) ReplayDurableVote( slot_key, replay_generation );
    }

    void ConsensusManager::ReleaseProposalAdmission( const Proposal &proposal, const std::string &slot_key )
    {
        std::lock_guard lock( proposals_mutex_ );
        const auto       slot = slot_states_.find( slot_key );
        const auto       active = slot != slot_states_.end() &&
                            ( slot->second.best_proposal_id == proposal.proposal_id() ||
                              slot->second.frozen_proposal_id == proposal.proposal_id() ||
                              slot->second.durable_proposal_id == proposal.proposal_id() ||
                              slot->second.reserved_finalization_proposal_id == proposal.proposal_id() ||
                              std::find( slot->second.late_candidate_ids.begin(),
                                         slot->second.late_candidate_ids.end(),
                                         proposal.proposal_id() ) != slot->second.late_candidate_ids.end() );
        if ( !active && pending_entries_.count( proposal.proposal_id() ) == 0 )
        {
            proposals_.erase( proposal.proposal_id() );
            pending_votes_.erase( proposal.proposal_id() );
        }
        resource_admissions_inflight_.erase( slot_key );
        slot_cv_.notify_all();
    }

    bool ConsensusManager::AdmitProposalResources( const Proposal &proposal, const std::string &slot_key )
    {
        {
            std::unique_lock lock( proposals_mutex_ );
            slot_cv_.wait( lock, [&]() { return resource_admissions_inflight_.count( slot_key ) == 0; } );
            if ( restored_final_slots_.count( slot_key ) != 0 || restored_safety_slots_.count( slot_key ) != 0 )
                return false;
            resource_admissions_inflight_.insert( slot_key );
        }

        ResourceAdmissionHandler handler;
        {
            auto type_hash = ParseSubjectTypeHash( proposal.subject() );
            if ( !type_hash )
            {
                ReleaseProposalAdmission( proposal, slot_key );
                return false;
            }
            std::shared_lock lock( resource_admission_handlers_mutex_ );
            auto it = resource_admission_handlers_.find( type_hash.value() );
            if ( it != resource_admission_handlers_.end() ) handler = it->second;
        }
        if ( !handler ) return true;

        auto descriptor = handler( proposal.subject(), slot_key );
        if ( descriptor.has_error() )
        {
            ReleaseProposalAdmission( proposal, slot_key );
            return false;
        }
        if ( !descriptor.value().has_value() ) return true;

        const auto window = timestamp_window_.count() < 0
                              ? 0ULL
                              : static_cast<uint64_t>( timestamp_window_.count() );
        const auto horizon = std::numeric_limits<uint64_t>::max() - proposal.timestamp() < window
                               ? std::numeric_limits<uint64_t>::max()
                               : proposal.timestamp() + window;
        auto admitted = state_store_->CreateOrJoinBurnReservation(
            slot_key, descriptor.value().value(), horizon, CurrentTimeMs() );
        if ( admitted.has_error() )
        {
            ReleaseProposalAdmission( proposal, slot_key );
            return false;
        }

        std::lock_guard lock( proposals_mutex_ );
        if ( restored_final_slots_.count( slot_key ) != 0 || restored_safety_slots_.count( slot_key ) != 0 )
        {
            resource_admissions_inflight_.erase( slot_key );
            slot_cv_.notify_all();
            return false;
        }
        return true;
    }

    void ConsensusManager::ReconcileBurnReservations()
    {
        auto activity = BeginActivity();
        if ( !activity || !state_store_ || stop_timer_.load() ) return;

        auto reservations = state_store_->ScanBurnReservations();
        if ( !reservations )
        {
            ConsensusManagerLogger()->critical( "{}: unable to scan durable burn reservations", __func__ );
            return;
        }

        const auto now = CurrentTimeMs();
        for ( const auto &record : reservations.value() )
        {
            if ( stop_timer_.load() ) return;
            if ( record.state() != ConsensusStateStore::BurnReservationRecord::RESERVED ||
                 now <= record.candidate_acceptance_horizon_ms() )
                continue;

            SlotState::Lifecycle prior_lifecycle = SlotState::Lifecycle::Empty;
            uint64_t prior_generation = 0;
            bool live_candidate_protected = false;
            {
                std::lock_guard lock( proposals_mutex_ );
                auto &slot = slot_states_[record.slot_id()];
                if ( resource_admissions_inflight_.count( record.slot_id() ) != 0 ||
                     slot.lifecycle == SlotState::Lifecycle::SigningPublishing ||
                     slot.lifecycle == SlotState::Lifecycle::PublishingReplay ||
                     slot.lifecycle == SlotState::Lifecycle::Finalizing ||
                     slot.lifecycle == SlotState::Lifecycle::Reconciling ||
                     restored_final_slots_.count( record.slot_id() ) != 0 ||
                     restored_safety_slots_.count( record.slot_id() ) != 0 )
                    continue;

                for ( const auto &[unused_id, proposal_state] : proposals_ )
                {
                    (void) unused_id;
                    if ( proposal_state.slot_key != record.slot_id() ) continue;
                    const auto window = timestamp_window_.count() < 0
                                          ? 0ULL
                                          : static_cast<uint64_t>( timestamp_window_.count() );
                    const auto proposal_horizon =
                        std::numeric_limits<uint64_t>::max() - proposal_state.proposal.timestamp() < window
                            ? std::numeric_limits<uint64_t>::max()
                            : proposal_state.proposal.timestamp() + window;
                    if ( now <= proposal_horizon )
                    {
                        live_candidate_protected = true;
                        break;
                    }
                }
                if ( live_candidate_protected ) continue;
                prior_lifecycle = slot.lifecycle;
                prior_generation = slot.generation;
                slot.lifecycle = SlotState::Lifecycle::Reconciling;
                resource_admissions_inflight_.insert( record.slot_id() );
            }

            const auto release_reconciliation = [&]( bool abandoned )
            {
                std::lock_guard lock( proposals_mutex_ );
                auto &slot = slot_states_[record.slot_id()];
                if ( slot.lifecycle == SlotState::Lifecycle::Reconciling )
                {
                    if ( abandoned )
                    {
                        for ( auto it = proposals_.begin(); it != proposals_.end(); )
                        {
                            if ( it->second.slot_key == record.slot_id() )
                            {
                                pending_votes_.erase( it->first );
                                it = proposals_.erase( it );
                            }
                            else
                                ++it;
                        }
                        slot = SlotState{};
                        slot.generation = prior_generation + 1;
                    }
                    else
                    {
                        slot.lifecycle = prior_lifecycle;
                        slot.generation = prior_generation;
                    }
                }
                resource_admissions_inflight_.erase( record.slot_id() );
                slot_cv_.notify_all();
            };

            if ( burn_reconciliation_stage_observer_ )
                burn_reconciliation_stage_observer_( "reserved", record.slot_id() );
            if ( stop_timer_.load() )
            {
                release_reconciliation( false );
                return;
            }

            auto certificate = GetCertificateBySlotId( record.slot_id() );
            if ( certificate )
            {
                release_reconciliation( false );
                (void) FinalizeSlot( certificate.value(), DeliverySource::Recovery );
                continue;
            }
            if ( certificate.error() != make_error_code( CertificateStoreError::NotFound ) )
            {
                release_reconciliation( false );
                continue;
            }

            auto vote = state_store_->GetVote( account_address_, record.slot_id() );
            if ( !vote )
            {
                release_reconciliation( false );
                continue;
            }
            if ( vote.value() && vote.value()->state() == ConsensusStateStore::VoteRecord::ACTIVE )
            {
                if ( now <= vote.value()->acceptance_horizon_ms() )
                {
                    release_reconciliation( false );
                    continue;
                }
                if ( !state_store_->RetireVote( account_address_, record.slot_id(), now ) )
                {
                    release_reconciliation( false );
                    continue;
                }
            }

            if ( burn_reconciliation_stage_observer_ )
                burn_reconciliation_stage_observer_( "before-delete", record.slot_id() );
            if ( stop_timer_.load() )
            {
                release_reconciliation( false );
                return;
            }

            // Recheck authoritative finality after every potentially blocking
            // durable operation. Only exact NotFound authorizes deletion.
            certificate = GetCertificateBySlotId( record.slot_id() );
            if ( certificate || certificate.error() != make_error_code( CertificateStoreError::NotFound ) )
            {
                release_reconciliation( false );
                if ( certificate ) (void) FinalizeSlot( certificate.value(), DeliverySource::Recovery );
                continue;
            }

            auto deleted = state_store_->DeleteReservedBurnReservation(
                record.slot_id(), record.generation(), record.candidate_acceptance_horizon_ms() );
            const bool abandoned = deleted && deleted.value() == ConsensusStateStore::BurnDeleteResult::Deleted;
            release_reconciliation( abandoned );
            if ( abandoned && burn_reconciliation_stage_observer_ )
                burn_reconciliation_stage_observer_( "deleted", record.slot_id() );
        }
    }

    void ConsensusManager::ProcessCandidateDeadlines( std::chrono::steady_clock::time_point steady_now )
    {
        struct FrozenCandidate
        {
            std::string slot;
            Proposal proposal;
            uint64_t generation;
        };
        std::vector<FrozenCandidate> frozen;
        {
            std::lock_guard lock( proposals_mutex_ );
            for ( auto &[slot, state] : slot_states_ )
            {
                if ( state.lifecycle != SlotState::Lifecycle::Selecting || steady_now < state.deadline ||
                     restored_final_slots_.count( slot ) != 0 || restored_safety_slots_.count( slot ) != 0 )
                    continue;
                auto proposal = proposals_.find( state.best_proposal_id );
                if ( proposal == proposals_.end() ) continue;
                state.lifecycle = SlotState::Lifecycle::SigningPublishing;
                state.frozen_proposal_id = state.best_proposal_id;
                frozen.push_back( { slot, proposal->second.proposal, state.generation } );
            }
        }

        const auto reservation_matches = [this]( const FrozenCandidate &candidate )
        {
            std::lock_guard lock( proposals_mutex_ );
            const auto it = slot_states_.find( candidate.slot );
            return it != slot_states_.end() &&
                   it->second.lifecycle == SlotState::Lifecycle::SigningPublishing &&
                   it->second.generation == candidate.generation &&
                   it->second.frozen_proposal_id == candidate.proposal.proposal_id() &&
                   restored_final_slots_.count( candidate.slot ) == 0 &&
                   restored_safety_slots_.count( candidate.slot ) == 0;
        };

        for ( const auto &candidate : frozen )
        {
            if ( vote_stage_observer_ ) vote_stage_observer_( "sign", candidate.slot, candidate.generation );
            if ( !reservation_matches( candidate ) ) continue;
            auto vote_result = CreateVote( candidate.proposal.proposal_id(), account_address_, true, signer_ );
            if ( !vote_result )
            {
                std::lock_guard lock( proposals_mutex_ );
                auto &state = slot_states_[candidate.slot];
                if ( state.lifecycle == SlotState::Lifecycle::SigningPublishing &&
                     state.generation == candidate.generation )
                {
                    state.lifecycle = SlotState::Lifecycle::Voted;
                    state.durable_proposal_id = candidate.proposal.proposal_id();
                    state.durable_generation = candidate.generation;
                    slot_cv_.notify_all();
                }
                continue;
            }

            ConsensusMessage envelope;
            *envelope.mutable_vote() = vote_result.value();
            std::string vote_bytes;
            std::string proposal_bytes;
            std::string envelope_bytes;
            if ( !vote_result.value().SerializeToString( &vote_bytes ) ||
                 !candidate.proposal.SerializeToString( &proposal_bytes ) ||
                 !envelope.SerializeToString( &envelope_bytes ) )
            {
                std::lock_guard lock( proposals_mutex_ );
                auto &state = slot_states_[candidate.slot];
                if ( state.lifecycle == SlotState::Lifecycle::SigningPublishing &&
                     state.generation == candidate.generation )
                {
                    state.lifecycle = SlotState::Lifecycle::Voted;
                    state.durable_proposal_id = candidate.proposal.proposal_id();
                    state.durable_generation = candidate.generation;
                    slot_cv_.notify_all();
                }
                continue;
            }

            const auto window = static_cast<uint64_t>( timestamp_window_.count() );
            const auto upper_bound = [window]( uint64_t timestamp )
            {
                return std::numeric_limits<uint64_t>::max() - timestamp < window
                           ? std::numeric_limits<uint64_t>::max()
                           : timestamp + window;
            };
            ConsensusStateStore::VoteRecord record;
            record.set_schema_version( 2 );
            record.set_state( ConsensusStateStore::VoteRecord::ACTIVE );
            record.set_slot_id( candidate.slot );
            record.set_proposal_id( candidate.proposal.proposal_id() );
            record.set_validator_id( account_address_ );
            record.set_signed_vote_bytes( vote_bytes );
            record.set_outbound_envelope_bytes( envelope_bytes );
            record.set_signed_proposal_bytes( proposal_bytes );
            record.set_registry_cid( candidate.proposal.registry_cid() );
            record.set_registry_epoch( candidate.proposal.registry_epoch() );
            record.set_generation( candidate.generation );
            record.set_created_at_ms( vote_result.value().timestamp() );
            record.set_acceptance_horizon_ms(
                std::min( upper_bound( candidate.proposal.timestamp() ),
                          upper_bound( vote_result.value().timestamp() ) ) );

            if ( !reservation_matches( candidate ) ) continue;
            if ( vote_stage_observer_ ) vote_stage_observer_( "put", candidate.slot, candidate.generation );
            auto put = vote_put_override_ ? vote_put_override_( record ) : state_store_->PutActiveVote( record );
            if ( !put )
            {
                std::lock_guard lock( proposals_mutex_ );
                auto &state = slot_states_[candidate.slot];
                if ( state.lifecycle == SlotState::Lifecycle::SigningPublishing &&
                     state.generation == candidate.generation )
                {
                    state.lifecycle = SlotState::Lifecycle::Voted;
                    state.durable_proposal_id = candidate.proposal.proposal_id();
                    state.durable_generation = candidate.generation;
                    state.last_publication_succeeded = false;
                    slot_cv_.notify_all();
                }
                continue;
            }

            if ( !reservation_matches( candidate ) ) continue;
            if ( vote_stage_observer_ ) vote_stage_observer_( "publish", candidate.slot, candidate.generation );
            auto published = PublishSerialized( record.outbound_envelope_bytes() );
            const auto publication_time = CurrentTimeMs();
            if ( !reservation_matches( candidate ) ) continue;
            (void) state_store_->UpdatePublication( account_address_, candidate.slot, publication_time,
                                                    published.has_value() );
            {
                std::lock_guard lock( proposals_mutex_ );
                auto &state = slot_states_[candidate.slot];
                if ( state.lifecycle == SlotState::Lifecycle::SigningPublishing &&
                     state.generation == candidate.generation )
                {
                    state.durable_proposal_id = candidate.proposal.proposal_id();
                    state.durable_generation = candidate.generation;
                    ++state.publication_count;
                    state.last_publication_at_ms = publication_time;
                    state.last_publication_succeeded = published.has_value();
                    state.lifecycle = SlotState::Lifecycle::Voted;
                    slot_cv_.notify_all();
                }
            }
            if ( published )
            {
                auto subject_hash = GetSubjectHash( candidate.proposal.subject() );
                if ( subject_hash )
                    EmitConsensusTrace( { ConsensusTraceEvent::Stage::VotePublished,
                                          record.validator_id(),
                                          candidate.slot,
                                          record.proposal_id(),
                                          subject_hash.value(),
                                          PublicPayloadDigest( record.outbound_envelope_bytes() ),
                                          std::nullopt } );
            }
            HandleVote( vote_result.value() );
        }
    }

    bool ConsensusManager::ReplayDurableVote( const std::string &slot_id, uint64_t generation )
    {
        auto stored = state_store_->GetVote( account_address_, slot_id );
        if ( !stored || !stored.value() || stored.value()->state() != ConsensusStateStore::VoteRecord::ACTIVE ||
             stored.value()->generation() != generation )
            return false;
        const auto record = stored.value().value();
        {
            std::lock_guard lock( proposals_mutex_ );
            auto it = slot_states_.find( slot_id );
            if ( it == slot_states_.end() || it->second.lifecycle != SlotState::Lifecycle::Voted ||
                 it->second.durable_generation != generation || restored_final_slots_.count( slot_id ) != 0 ||
                 restored_safety_slots_.count( slot_id ) != 0 )
                return false;
            it->second.lifecycle = SlotState::Lifecycle::PublishingReplay;
        }
        if ( vote_stage_observer_ ) vote_stage_observer_( "replay", slot_id, generation );
        auto published = PublishSerialized( record.outbound_envelope_bytes() );
        const auto publication_time = CurrentTimeMs();
        {
            std::lock_guard lock( proposals_mutex_ );
            auto it = slot_states_.find( slot_id );
            if ( it == slot_states_.end() || it->second.lifecycle != SlotState::Lifecycle::PublishingReplay ||
                 it->second.durable_generation != generation )
                return false;
        }
        (void) state_store_->UpdatePublication( account_address_, slot_id, publication_time, published.has_value() );
        {
            std::lock_guard lock( proposals_mutex_ );
            auto it = slot_states_.find( slot_id );
            if ( it != slot_states_.end() && it->second.lifecycle == SlotState::Lifecycle::PublishingReplay &&
                 it->second.durable_generation == generation )
            {
                ++it->second.publication_count;
                it->second.last_publication_at_ms = publication_time;
                it->second.last_publication_succeeded = published.has_value();
                it->second.lifecycle = SlotState::Lifecycle::Voted;
                slot_cv_.notify_all();
            }
        }
        if ( published )
        {
            Proposal proposal;
            if ( proposal.ParseFromString( record.signed_proposal_bytes() ) )
            {
                auto subject_hash = GetSubjectHash( proposal.subject() );
                if ( subject_hash )
                    EmitConsensusTrace( { ConsensusTraceEvent::Stage::VotePublished,
                                          record.validator_id(),
                                          slot_id,
                                          record.proposal_id(),
                                          subject_hash.value(),
                                          PublicPayloadDigest( record.outbound_envelope_bytes() ),
                                          std::nullopt } );
            }
        }
        return published.has_value();
    }

    bool ConsensusManager::AddPendingProposal( const Proposal                       &proposal,
                                               const std::string                    &subject_hash,
                                               const ValidationResult               &validation_result,
                                               std::size_t                           scheduled_retry_count,
                                               std::chrono::steady_clock::time_point last_retry_at )
    {
        ConsensusManagerLogger()->debug( "{}: Adding pending proposal for hash {} proposal_id={}",
                                         __func__,
                                         GetPrintableSubjectHash( proposal.subject() ),
                                         proposal.proposal_id().substr( 0, 8 ) );

        std::lock_guard lock( proposals_mutex_ );
        if ( pending_entries_.find( proposal.proposal_id() ) != pending_entries_.end() )
        {
            RemovePendingProposalLocked( proposal.proposal_id(), "replace" );
        }
        auto [proposal_it, inserted] = proposals_.try_emplace( proposal.proposal_id() );
        if ( inserted )
        {
            auto slot_result = GetSlotKey( proposal );
            if ( slot_result.has_error() )
            {
                proposals_.erase( proposal_it );
                ConsensusManagerLogger()->error( "{}: rejected: canonical slot derivation failed proposal_id={}",
                                                 __func__,
                                                 proposal.proposal_id().substr( 0, 8 ) );
                return false;
            }
            proposal_it->second.proposal = proposal;
            proposal_it->second.slot_key = slot_result.value();
        }

        const auto  retained_bytes = static_cast<std::size_t>( proposal.ByteSizeLong() );
        const auto &proposer_id    = proposal.proposer_id();
        if ( pending_entries_.size() >= pending_config_.max_pending_proposals )
        {
            ConsensusManagerLogger()->warn( "{}: pending admission refused: global limit reached proposal_id={}",
                                            __func__,
                                            proposal.proposal_id().substr( 0, 8 ) );
            return false;
        }
        auto proposer_it = pending_count_by_proposer_.find( proposer_id );
        if ( proposer_it != pending_count_by_proposer_.end() &&
             proposer_it->second >= pending_config_.max_pending_per_proposer )
        {
            ConsensusManagerLogger()->warn(
                "{}: pending admission refused: proposer limit reached proposer={} proposal_id={}",
                __func__,
                proposer_id.substr( 0, 8 ),
                proposal.proposal_id().substr( 0, 8 ) );
            return false;
        }
        if ( pending_retained_bytes_ + retained_bytes > pending_config_.max_retained_pending_bytes )
        {
            ConsensusManagerLogger()->warn( "{}: pending admission refused: retained byte limit reached proposal_id={}",
                                            __func__,
                                            proposal.proposal_id().substr( 0, 8 ) );
            return false;
        }

        auto dependencies = validation_result.dependencies;
        if ( dependencies.empty() )
        {
            dependencies.emplace_back( PendingDependencyKey::Certificate( subject_hash ) );
        }

        ConsensusManagerLogger()->debug( "{}: Adding pending proposal for {}: proposal with id {}",
                                         __func__,
                                         subject_hash.substr( 0, 8 ),
                                         proposal.proposal_id().substr( 0, 8 ) );
        const auto now         = std::chrono::steady_clock::now();
        auto       retry_delay = validation_result.retry_after.value_or( std::chrono::seconds( 10 ) );
        if ( !validation_result.retry_after && !pending_config_.scheduled_retry_delays.empty() )
        {
            const auto index = std::min( scheduled_retry_count, pending_config_.scheduled_retry_delays.size() - 1 );
            retry_delay      = pending_config_.scheduled_retry_delays[index];
        }

        PendingProposalEntry entry;
        entry.proposal              = proposal;
        entry.dependencies          = std::move( dependencies );
        entry.admitted_at           = now;
        entry.expires_at            = now + pending_config_.pending_ttl;
        entry.last_retry_at         = last_retry_at;
        entry.retry_after           = validation_result.retry_after;
        entry.retained_bytes        = retained_bytes;
        entry.proposer_id           = proposer_id;
        entry.scheduled_retry_count = scheduled_retry_count;
        entry.next_retry_at         = now + retry_delay;

        pending_retained_bytes_                 += retained_bytes;
        pending_count_by_proposer_[proposer_id] += 1;
        for ( const auto &dependency : entry.dependencies )
        {
            pending_by_dependency_[dependency].insert( proposal.proposal_id() );
        }
        pending_entries_.emplace( proposal.proposal_id(), std::move( entry ) );
        timer_cv_.notify_all();
        return true;
    }

    std::vector<ConsensusManager::Proposal> ConsensusManager::TakePendingProposals( const std::string &subject_hash )
    {
        std::vector<Proposal> result;
        std::lock_guard       lock( proposals_mutex_ );
        const auto            dependency = PendingDependencyKey::Certificate( subject_hash );
        auto                  it         = pending_by_dependency_.find( dependency );
        if ( it == pending_by_dependency_.end() )
        {
            ConsensusManagerLogger()->trace( "{}: No pending proposals for {}", __func__, subject_hash.substr( 0, 8 ) );
            return result;
        }
        auto proposal_ids = std::move( it->second );
        pending_by_dependency_.erase( it );
        for ( const auto &proposal_id : proposal_ids )
        {
            auto prop_it = pending_entries_.find( proposal_id );
            if ( prop_it != pending_entries_.end() )
            {
                result.push_back( prop_it->second.proposal );
                RemovePendingProposalLocked( proposal_id, "take" );
            }
        }
        ConsensusManagerLogger()->debug( "{}: Taking pending proposals for {}", __func__, subject_hash.substr( 0, 8 ) );
        return result;
    }

    bool ConsensusManager::RemovePendingProposal( const std::string &proposal_id, std::string_view reason )
    {
        std::lock_guard lock( proposals_mutex_ );
        return RemovePendingProposalLocked( proposal_id, reason );
    }

    bool ConsensusManager::RemovePendingProposalLocked( const std::string &proposal_id, std::string_view reason )
    {
        pending_votes_.erase( proposal_id );

        auto entry_it = pending_entries_.find( proposal_id );
        if ( entry_it == pending_entries_.end() )
        {
            return false;
        }

        const auto &entry        = entry_it->second;
        pending_retained_bytes_ -= std::min( pending_retained_bytes_, entry.retained_bytes );

        auto proposer_it = pending_count_by_proposer_.find( entry.proposer_id );
        if ( proposer_it != pending_count_by_proposer_.end() )
        {
            if ( proposer_it->second > 1 )
            {
                --proposer_it->second;
            }
            else
            {
                pending_count_by_proposer_.erase( proposer_it );
            }
        }

        for ( const auto &dependency : entry.dependencies )
        {
            auto dep_it = pending_by_dependency_.find( dependency );
            if ( dep_it == pending_by_dependency_.end() )
            {
                continue;
            }
            dep_it->second.erase( proposal_id );
            if ( dep_it->second.empty() )
            {
                pending_by_dependency_.erase( dep_it );
            }
        }

        pending_entries_.erase( entry_it );
        ConsensusManagerLogger()->debug( "{}: removed pending proposal_id={} reason={}",
                                         __func__,
                                         proposal_id.substr( 0, 8 ),
                                         reason );
        return true;
    }

    void ConsensusManager::RetryPendingProposal( const Proposal                       &proposal,
                                                 std::string_view                      reason,
                                                 std::size_t                           scheduled_retry_count,
                                                 std::chrono::steady_clock::time_point last_retry_at )
    {
        auto type_hash = ParseSubjectTypeHash( proposal.subject() );
        if ( !type_hash )
        {
            ConsensusManagerLogger()->error( "{}: rejected: invalid subject type hash reason={}", __func__, reason );
            return;
        }
        auto slot_result = GetSlotKey( proposal );
        if ( slot_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: canonical slot derivation failed proposal_id={} reason={}",
                                             __func__,
                                             proposal.proposal_id().substr( 0, 8 ),
                                             reason );
            return;
        }

        SubjectHandler subject_handler;
        {
            std::shared_lock lock( subject_handlers_mutex_ );
            auto             handler_it = subject_handlers_.find( type_hash.value() );
            if ( handler_it == subject_handlers_.end() )
            {
                ConsensusManagerLogger()->error(
                    "{}: rejected: subject handler missing type_hash={} reason={}",
                    __func__,
                    base::hex_lower( gsl::span<const uint8_t>(
                        reinterpret_cast<const uint8_t *>( proposal.subject().subject_type_hash().hash().data() ),
                        proposal.subject().subject_type_hash().hash().size() ) ),
                    reason );
                return;
            }
            subject_handler = handler_it->second;
        }

        auto subject_result = subject_handler( proposal.subject() );
        if ( subject_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: subject handler error proposal_id={} reason={}",
                                             __func__,
                                             proposal.proposal_id().substr( 0, 8 ),
                                             reason );
            return;
        }

        const auto &validation_result = subject_result.value();
        if ( validation_result.check == Check::Reject )
        {
            ConsensusManagerLogger()->error( "{}: rejected: subject check failed proposal_id={} reason={}",
                                             __func__,
                                             proposal.proposal_id().substr( 0, 8 ),
                                             reason );
            return;
        }

        if ( validation_result.check == Check::Stalled )
        {
            ConsensusManagerLogger()->warn( "{}: stalled: subject handler stalled proposal_id={} reason={}",
                                            __func__,
                                            proposal.proposal_id().substr( 0, 8 ),
                                            reason );
            return;
        }

        if ( validation_result.check == Check::Pending )
        {
            auto subject_hash_result = GetSubjectHash( proposal.subject() );
            if ( subject_hash_result.has_error() )
            {
                ConsensusManagerLogger()->error( "{}: rejected: subject hash missing proposal_id={} reason={}",
                                                 __func__,
                                                 proposal.proposal_id().substr( 0, 8 ),
                                                 reason );
                return;
            }
            AddPendingProposal( proposal,
                                subject_hash_result.value(),
                                validation_result,
                                scheduled_retry_count,
                                last_retry_at );
            return;
        }

        if ( !AdmitProposalResources( proposal, slot_result.value() ) ) return;
        ContinueProposalAfterSubject( proposal, slot_result.value() );
    }

    outcome::result<void> ConsensusManager::WakePendingDependency( const PendingDependencyKey &dependency )
    {
        struct DependencyRetryCandidate
        {
            Proposal    proposal;
            std::size_t scheduled_retry_count = 0;
        };

        std::vector<DependencyRetryCandidate> retry_now;
        const auto                            now = std::chrono::steady_clock::now();
        {
            std::lock_guard lock( proposals_mutex_ );
            auto            dep_it = pending_by_dependency_.find( dependency );
            if ( dep_it == pending_by_dependency_.end() )
            {
                return outcome::success();
            }

            const std::vector<std::string> proposal_ids( dep_it->second.begin(), dep_it->second.end() );
            for ( const auto &proposal_id : proposal_ids )
            {
                auto entry_it = pending_entries_.find( proposal_id );
                if ( entry_it == pending_entries_.end() )
                {
                    continue;
                }
                if ( now >= entry_it->second.expires_at )
                {
                    continue;
                }
                if ( entry_it->second.last_retry_at != std::chrono::steady_clock::time_point{} &&
                     now - entry_it->second.last_retry_at < pending_config_.min_dependency_retry_interval )
                {
                    entry_it->second.next_retry_at = entry_it->second.last_retry_at +
                                                     pending_config_.min_dependency_retry_interval;
                    continue;
                }

                DependencyRetryCandidate candidate;
                candidate.proposal              = entry_it->second.proposal;
                candidate.scheduled_retry_count = entry_it->second.scheduled_retry_count;
                entry_it->second.last_retry_at  = now;
                retry_now.push_back( std::move( candidate ) );
                RemovePendingProposalLocked( proposal_id, "dependency-wake" );
            }
        }

        for ( const auto &candidate : retry_now )
        {
            RetryPendingProposal( candidate.proposal, "dependency-wake", candidate.scheduled_retry_count, now );
        }
        return outcome::success();
    }

    void ConsensusManager::ProcessDuePendingRetries()
    {
        struct RetryCandidate
        {
            Proposal    proposal;
            std::size_t scheduled_retry_count = 0;
        };

        std::vector<RetryCandidate> retry_now;
        const auto                  now = std::chrono::steady_clock::now();
        {
            std::lock_guard lock( proposals_mutex_ );
            for ( auto it = pending_entries_.begin(); it != pending_entries_.end(); )
            {
                if ( now < it->second.next_retry_at || now >= it->second.expires_at )
                {
                    ++it;
                    continue;
                }

                const auto     proposal_id = it->first;
                RetryCandidate candidate;
                candidate.proposal              = it->second.proposal;
                candidate.scheduled_retry_count = it->second.scheduled_retry_count + 1;
                it->second.last_retry_at        = now;
                retry_now.push_back( std::move( candidate ) );
                ++it;
                RemovePendingProposalLocked( proposal_id, "scheduled-retry" );
            }
        }

        for ( const auto &candidate : retry_now )
        {
            RetryPendingProposal( candidate.proposal, "scheduled-retry", candidate.scheduled_retry_count, now );
        }
    }

    void ConsensusManager::ExpirePendingProposals()
    {
        std::vector<Proposal> expired;
        const auto            now = std::chrono::steady_clock::now();
        {
            std::lock_guard lock( proposals_mutex_ );
            for ( auto it = pending_entries_.begin(); it != pending_entries_.end(); )
            {
                if ( now < it->second.expires_at )
                {
                    ++it;
                    continue;
                }
                const auto proposal_id = it->first;
                auto       proposal = it->second.proposal;
                ++it;
                if ( RemovePendingProposalLocked( proposal_id, "ttl-expired" ) )
                    expired.push_back( std::move( proposal ) );
            }
        }

        for ( const auto &proposal : expired )
        {
            {
                std::lock_guard lock( proposals_mutex_ );
                proposals_.erase( proposal.proposal_id() );
                pending_votes_.erase( proposal.proposal_id() );
            }
            FireProposalCleanupCallbacks( proposal );
        }
    }

    outcome::result<ConsensusManager::Proposal> ConsensusManager::CreateProposal( const Subject     &subject,
                                                                                  const std::string &proposer_id,
                                                                                  const std::string &registry_cid,
                                                                                  uint64_t           registry_epoch )
    {
        return CreateProposal( subject, proposer_id, registry_cid, registry_epoch, signer_ );
    }

    outcome::result<ConsensusManager::Proposal> ConsensusManager::CreateProposal( const Subject     &subject,
                                                                                  const std::string &proposer_id,
                                                                                  const std::string &registry_cid,
                                                                                  uint64_t           registry_epoch,
                                                                                  Signer             sign )
    {
        ConsensusManagerLogger()->trace( "{}: called by {} with hash {}, registry CID {} and epoch {}",
                                         __func__,
                                         proposer_id.substr( 0, 8 ),
                                         GetPrintableSubjectHash( subject ),
                                         registry_cid,
                                         registry_epoch );

        if ( !ValidateSubject( subject ) )
        {
            ConsensusManagerLogger()->error( "{}: failed for hash {}: subject validation failed",
                                             __func__,
                                             GetPrintableSubjectHash( subject ) );
            return outcome::failure( std::errc::invalid_argument );
        }

        Proposal proposal;
        *proposal.mutable_subject() = subject;
        proposal.set_proposer_id( proposer_id );
        proposal.set_registry_cid( registry_cid );
        proposal.set_registry_epoch( registry_epoch );
        proposal.set_timestamp( CurrentTimeMs() );

        proposal.set_proposal_id( CreateProposalId( proposal ) );
        auto signing_bytes = sgns::ProposalSigningBytes( proposal );
        if ( signing_bytes.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed: signing bytes error={}",
                                             __func__,
                                             signing_bytes.error().message() );
            return outcome::failure( signing_bytes.error() );
        }
        ConsensusManagerLogger()->debug( "{}: Creating proposal ID {} for hash {}",
                                         __func__,
                                         proposal.proposal_id().substr( 0, 8 ),
                                         GetPrintableSubjectHash( subject ) );
        BOOST_OUTCOME_TRY( auto &&signature, sign( signing_bytes.value() ) );
        proposal.set_signature( signature.data(), signature.size() );

        ConsensusManagerLogger()->debug( "{}: success for hash {} proposal_id={}",
                                         __func__,
                                         GetPrintableSubjectHash( subject ),
                                         proposal.proposal_id().substr( 0, 8 ) );
        return proposal;
    }

    outcome::result<ConsensusManager::Vote> ConsensusManager::CreateVote( const std::string &proposal_id,
                                                                          const std::string &voter_id,
                                                                          bool               approve,
                                                                          Signer             sign )
    {
        Vote vote;
        vote.set_proposal_id( proposal_id );
        vote.set_voter_id( voter_id );
        vote.set_approve( approve );
        vote.set_timestamp( CurrentTimeMs() );

        // Phase 6 (D-01): populate slot_N_hash fields before signing so the
        // signature commits to them (T-06-01). No-op when no populator is set.
        SlotHashPopulator slot_hash_populator;
        {
            std::lock_guard<std::mutex> lock( slot_hash_populator_mutex_ );
            slot_hash_populator = slot_hash_populator_;
        }
        if ( slot_hash_populator )
        {
            slot_hash_populator( vote );
            ConsensusManagerLogger()->debug( "{}: populated slot hashes for proposal_id={}",
                                             __func__,
                                             proposal_id.substr( 0, 8 ) );
        }

        auto signing_bytes = sgns::VoteSigningBytes( vote );
        if ( signing_bytes.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed: signing bytes error={}",
                                             __func__,
                                             signing_bytes.error().message() );
            return outcome::failure( signing_bytes.error() );
        }

        BOOST_OUTCOME_TRY( auto &&signature, sign( signing_bytes.value() ) );
        vote.set_signature( signature.data(), signature.size() );

        ConsensusManagerLogger()->debug( "{}: {} voted for proposal_id={}",
                                         __func__,
                                         voter_id.substr( 0, 8 ),
                                         proposal_id.substr( 0, 8 ) );
        return vote;
    }

    outcome::result<ConsensusManager::Certificate> ConsensusManager::CreateCertificate( const Proposal &proposal,
                                                                                        const std::vector<Vote> &votes )
    {
        ConsensusManagerLogger()->trace(
            "{}: Creating certificate for hash {}: proposal_id={} number of votes={} registry CID={}, epoch={}",
            __func__,
            GetPrintableSubjectHash( proposal.subject() ),
            proposal.proposal_id().substr( 0, 8 ),
            votes.size(),
            proposal.registry_cid(),
            proposal.registry_epoch() );
        auto slot_result = GetSlotKey( proposal );
        if ( !slot_result ) return outcome::failure( slot_result.error() );
        {
            std::lock_guard lock( proposals_mutex_ );
            if ( restored_safety_slots_.count( slot_result.value() ) != 0 )
                return outcome::failure( CertificateStoreError::Conflict );
        }
        auto tally_result = TallyVotes( proposal, votes );
        if ( tally_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed: tally error={}", __func__, tally_result.error().message() );
            return outcome::failure( tally_result.error() );
        }

        const auto &tally = tally_result.value();
        if ( !tally.has_quorum || votes.empty() )
        {
            ConsensusManagerLogger()->error( "{}: failed: certificate requires at least one valid quorum vote",
                                             __func__ );
            return outcome::failure( CertificateStoreError::InvalidCertificate );
        }

        std::vector<Vote> canonical_votes = votes;
        std::sort( canonical_votes.begin(), canonical_votes.end(), VoterIdBytewiseLess );

        Certificate cert;
        cert.set_proposal_id( proposal.proposal_id() );
        cert.set_registry_cid( proposal.registry_cid() );
        cert.set_registry_epoch( proposal.registry_epoch() );
        cert.set_total_weight( tally.total_weight );
        cert.set_approved_weight( tally.approved_weight );
        uint64_t max_vote_ts = 0;
        for ( const auto &vote : canonical_votes )
        {
            max_vote_ts = std::max( vote.timestamp(), max_vote_ts );
        }
        if ( max_vote_ts == 0 )
        {
            ConsensusManagerLogger()->error( "{}: failed: certificate votes have no timestamp", __func__ );
            return outcome::failure( CertificateStoreError::InvalidCertificate );
        }
        cert.set_timestamp( max_vote_ts );
        for ( const auto &vote : canonical_votes )
        {
            *cert.add_votes() = vote;
        }
        *cert.mutable_proposal() = proposal;

        auto normalized = NormalizeCertificateStructural( cert );
        if ( normalized.check != Check::Approve )
        {
            return outcome::failure( CertificateStoreError::InvalidCertificate );
        }

        ConsensusManagerLogger()->debug( "{}: Success creating certificate for hash {} proposal_id={}",
                                         __func__,
                                         GetPrintableSubjectHash( proposal.subject() ),
                                         proposal.proposal_id().substr( 0, 8 ) );
        return std::move( normalized.certificate );
    }

    bool ConsensusManager::IsBridgeMintSubject( const Proposal &proposal )
    {
        // Fail-closed (RESEARCH Pattern 2): any decode failure returns false so
        // the single-pool IsQuorum path applies. Only a successfully-decoded
        // NonceSubject carrying a kMintV2 with public-chain metadata is a
        // bridge mint. Test/local chains can still use a registered
        // IInputValidator without being forced through RPC slot quorum.
        const auto nonce_payload = DecodeNonceSubject( proposal.subject() );
        if ( nonce_payload.has_error() )
        {
            return false;
        }
        const auto &transaction = nonce_payload.value().transaction();
        if ( transaction.transaction_case() != EmbeddedTransaction::kMintV2 )
        {
            return false;
        }
        const auto &mint = transaction.mint_v2();
        return IsPublicChainMintChainId( mint.chain_id() );
    }

    outcome::result<ConsensusManager::QuorumTally> ConsensusManager::EvaluateQuorum(
        const Proposal                    &proposal,
        const std::vector<Vote>           &votes,
        const ValidatorRegistry::Registry &registry ) const
    {
        QuorumTally tally;

        if ( IsBridgeMintSubject( proposal ) )
        {
            // Bridge-mint subject: cumulative slot tally (D-06).
            const auto slot_result = registry_->EvaluateSlotQuorum( votes, registry );
            tally.total_weight     = ValidatorRegistry::TotalWeight( registry );
            tally.approved_weight  = slot_result.total_voting_reputation;
            tally.has_quorum       = slot_result.has_quorum;
            tally.qualified_sum    = slot_result.qualified_sum;
            tally.slot_threshold   = slot_result.threshold;
            ConsensusManagerLogger()->debug( "{}: bridge-mint slot tally hash {} proposal_id={} qualified_sum={} "
                                             "threshold={} total_voting_rep={} has_quorum={}",
                                             __func__,
                                             GetPrintableSubjectHash( proposal.subject() ),
                                             proposal.proposal_id().substr( 0, 8 ),
                                             slot_result.qualified_sum,
                                             slot_result.threshold,
                                             slot_result.total_voting_reputation,
                                             slot_result.has_quorum );
            return tally;
        }

        // Non-bridge subject: unchanged single-pool IsQuorum path.
        const uint64_t                  total_weight    = ValidatorRegistry::TotalWeight( registry );
        uint64_t                        approved_weight = 0;
        std::unordered_set<std::string> seen;
        for ( const auto &vote : votes )
        {
            if ( vote.proposal_id() != proposal.proposal_id() )
            {
                continue;
            }
            if ( !seen.insert( vote.voter_id() ).second )
            {
                continue;
            }
            const auto *validator = ValidatorRegistry::FindValidator( registry, vote.voter_id() );
            if ( !validator || validator->status() != ValidatorRegistry::Status::ACTIVE )
            {
                continue;
            }
            if ( vote.approve() )
            {
                approved_weight += validator->weight();
            }
        }
        tally.total_weight    = total_weight;
        tally.approved_weight = approved_weight;
        tally.has_quorum      = registry_->IsQuorum( approved_weight, total_weight );
        // qualified_sum / slot_threshold remain zero for non-bridge (observability).
        return tally;
    }

    outcome::result<ConsensusManager::QuorumTally> ConsensusManager::TallyVotes(
        const Proposal                    &proposal,
        const std::vector<Vote>           &votes,
        const ValidatorRegistry::Registry &registry,
        const std::string                 &registry_cid ) const
    {
        if ( !proposal.registry_cid().empty() && !registry_cid.empty() && proposal.registry_cid() != registry_cid )
        {
            ConsensusManagerLogger()->error(
                "{}: failed: registry cid mismatch hash {}, proposal CID ={} registry CID={}",
                __func__,
                GetPrintableSubjectHash( proposal.subject() ),
                proposal.registry_cid(),
                registry_cid );
            return outcome::failure( std::errc::invalid_argument );
        }
        if ( proposal.registry_epoch() != registry.epoch() )
        {
            ConsensusManagerLogger()->error(
                "{}: failed: registry epoch mismatch hash {}, proposal Epoch={} registry Epoch={}",
                __func__,
                GetPrintableSubjectHash( proposal.subject() ),
                proposal.registry_epoch(),
                registry.epoch() );
            return outcome::failure( std::errc::invalid_argument );
        }

        uint64_t                        total_weight    = ValidatorRegistry::TotalWeight( registry );
        uint64_t                        approved_weight = 0;
        std::unordered_set<std::string> seen;
        std::vector<Vote>               verified_votes;
        verified_votes.reserve( votes.size() );

        for ( const auto &vote : votes )
        {
            ConsensusManagerLogger()->trace( "{}: processing vote for hash {}: voter_id={} approve={}",
                                             __func__,
                                             GetPrintableSubjectHash( proposal.subject() ),
                                             vote.voter_id().substr( 0, 8 ),
                                             vote.approve() );
            if ( vote.proposal_id() != proposal.proposal_id() )
            {
                ConsensusManagerLogger()->error( "{}: failed: vote proposal mismatch voter_id={}",
                                                 __func__,
                                                 vote.voter_id() );
                return outcome::failure( CertificateStoreError::InvalidCertificate );
            }
            if ( !seen.insert( vote.voter_id() ).second )
            {
                ConsensusManagerLogger()->error( "{}: failed: duplicate vote voter_id={}",
                                                 __func__,
                                                 vote.voter_id() );
                return outcome::failure( CertificateStoreError::InvalidCertificate );
            }

            const auto *validator = ValidatorRegistry::FindValidator( registry, vote.voter_id() );
            if ( !validator || validator->status() != ValidatorRegistry::Status::ACTIVE )
            {
                ConsensusManagerLogger()->error( "{}: failed: voter is not an active registry validator voter_id={}",
                                                 __func__,
                                                 vote.voter_id() );
                return outcome::failure( CertificateStoreError::InvalidCertificate );
            }

            auto signing_bytes = sgns::VoteSigningBytes( vote );
            if ( signing_bytes.has_error() )
            {
                return outcome::failure( CertificateStoreError::InvalidCertificate );
            }

            if ( !GeniusAccount::VerifySignature( vote.voter_id(), vote.signature(), signing_bytes.value() ) )
            {
                ConsensusManagerLogger()->error( "{}: failed: invalid vote signature voter_id={}",
                                                 __func__,
                                                 vote.voter_id() );
                return outcome::failure( CertificateStoreError::InvalidCertificate );
            }

            ConsensusManagerLogger()->debug( "{}: Valid voter signature for hash {}: voter_id={} approve={}",
                                             __func__,
                                             GetPrintableSubjectHash( proposal.subject() ),
                                             vote.voter_id().substr( 0, 8 ),
                                             vote.approve() );
            verified_votes.push_back( vote );
            if ( vote.approve() )
            {
                ConsensusManagerLogger()->debug( "{}: Adding weight for hash {}: voter_id={} weight={}",
                                                 __func__,
                                                 GetPrintableSubjectHash( proposal.subject() ),
                                                 vote.voter_id().substr( 0, 8 ),
                                                 validator->weight() );
                approved_weight += validator->weight();
            }
        }

        QuorumTally tally;
        tally.total_weight    = total_weight;
        tally.approved_weight = approved_weight;
        // Phase 6 (D-06): route the final has_quorum decision through the
        // shared EvaluateQuorum dispatcher so bridge-mint subjects use the
        // cumulative slot model. The non-bridge branch recomputes the same
        // single-pool IsQuorum result; sig verification stays in this loop.
        // Both TallyVotes (certificate creation) and the incremental HandleVote
        // tally agree on bridge-mint quorum via this single helper (Pitfall 1).
        auto quorum_result = EvaluateQuorum( proposal, verified_votes, registry );
        if ( quorum_result.has_error() )
        {
            return quorum_result.error();
        }
        tally.has_quorum     = quorum_result.value().has_quorum;
        tally.qualified_sum  = quorum_result.value().qualified_sum;
        tally.slot_threshold = quorum_result.value().slot_threshold;
        ConsensusManagerLogger()->debug(
            "{}: Votes tallied for hash {} proposal_id={} approved_weight={} total_weight={} quorum={}",
            __func__,
            GetPrintableSubjectHash( proposal.subject() ),
            proposal.proposal_id().substr( 0, 8 ),
            approved_weight,
            total_weight,
            tally.has_quorum );
        return tally;
    }

    outcome::result<ConsensusManager::QuorumTally> ConsensusManager::TallyVotes( const Proposal          &proposal,
                                                                                 const std::vector<Vote> &votes ) const
    {
        ConsensusManagerLogger()->trace(
            "{}: Tallying with current registry for hash {}, proposal_id={} number of votes={}",
            __func__,
            GetPrintableSubjectHash( proposal.subject() ),
            proposal.proposal_id().substr( 0, 8 ),
            votes.size() );

        if ( proposal.registry_cid().empty() )
        {
            ConsensusManagerLogger()->error( "{}: failed: proposal registry CID is empty", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }

        auto registry_result = registry_->LoadRegistryByCid( proposal.registry_cid() );
        if ( registry_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed: registry load error={} cid={}",
                                             __func__,
                                             registry_result.error().message(),
                                             proposal.registry_cid() );
            return outcome::failure( registry_result.error() );
        }
        return TallyVotes( proposal, votes, registry_result.value(), proposal.registry_cid() );
    }

    outcome::result<void> ConsensusManager::SubmitProposal( const Proposal &proposal, bool self_vote )
    {
        ConsensusManagerLogger()->trace( "{}: called for hash {} proposal_id={} self_vote={}",
                                         __func__,
                                         GetPrintableSubjectHash( proposal.subject() ),
                                         proposal.proposal_id().substr( 0, 8 ),
                                         self_vote );
        auto slot_result = GetSlotKey( proposal );
        if ( slot_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: canonical slot derivation failed proposal_id={}",
                                             __func__,
                                             proposal.proposal_id().substr( 0, 8 ) );
            return outcome::failure( slot_result.error() );
        }
        const auto &slot_key = slot_result.value();
        auto subject_hash = GetSubjectHash( proposal.subject() );
        {
            std::lock_guard lock( proposals_mutex_ );
            if ( restored_safety_slots_.count( slot_key ) != 0 )
                return outcome::failure( std::errc::operation_not_permitted );
            auto            it = proposals_.find( proposal.proposal_id() );
            if ( it == proposals_.end() )
            {
                ConsensusManagerLogger()->debug( "{}: Creating proposal state for hash {} proposal_id={}",
                                                 __func__,
                                                 GetPrintableSubjectHash( proposal.subject() ),
                                                 proposal.proposal_id().substr( 0, 8 ) );
                ProposalState state;
                state.proposal = proposal;
                state.slot_key = slot_key;
                proposals_.emplace( proposal.proposal_id(), std::move( state ) );
            }
        }

        ConsensusMessage message;
        *message.mutable_proposal() = proposal;
        auto publish_result         = Publish( message );
        if ( publish_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed: publish error={}",
                                             __func__,
                                             publish_result.error().message() );
            return publish_result;
        }
        ConsensusManagerLogger()->debug( "{}: success for hash {} proposal_id={}",
                                         __func__,
                                         GetPrintableSubjectHash( proposal.subject() ),
                                         proposal.proposal_id().substr( 0, 8 ) );

        if ( subject_hash )
        {
            std::string envelope_bytes;
            if ( message.SerializeToString( &envelope_bytes ) )
                EmitConsensusTrace( { ConsensusTraceEvent::Stage::LocalProposalPublished,
                                      account_address_,
                                      slot_key,
                                      proposal.proposal_id(),
                                      subject_hash.value(),
                                      PublicPayloadDigest( envelope_bytes ),
                                      std::nullopt } );
        }

        if ( self_vote )
        {
            HandleProposal( proposal );
        }

        return outcome::success();
    }

    outcome::result<void> ConsensusManager::SubmitVote( const Vote &vote, bool self_handle )
    {
        ConsensusManagerLogger()->trace( "{}: called by {} proposal_id={}",
                                         __func__,
                                         vote.voter_id().substr( 0, 8 ),
                                         vote.proposal_id().substr( 0, 8 ) );
        {
            std::lock_guard lock( proposals_mutex_ );
            if ( restored_safety_proposal_ids_.count( vote.proposal_id() ) != 0 )
                return outcome::failure( std::errc::operation_not_permitted );
            auto proposal = proposals_.find( vote.proposal_id() );
            if ( proposal != proposals_.end() &&
                 restored_safety_slots_.count( proposal->second.slot_key ) != 0 )
                return outcome::failure( std::errc::operation_not_permitted );
        }
        ConsensusMessage message;
        *message.mutable_vote() = vote;
        auto result             = Publish( message );
        if ( result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed: publish error={}", __func__, result.error().message() );
            return result;
        }
        ConsensusManagerLogger()->debug( "{}: success voter_id={} proposal_id={} ",
                                         __func__,
                                         vote.voter_id().substr( 0, 8 ),
                                         vote.proposal_id().substr( 0, 8 ) );
        if ( self_handle )
        {
            HandleVote( vote );
        }
        return result;
    }

    outcome::result<void> ConsensusManager::SubmitCertificate( const Certificate &certificate )
    {
        // Preserve the public store API's distinction between integrity and
        // operational read failures. FinalizeSlot intentionally collapses
        // storage failures into its source-independent typed result.
        auto normalized = NormalizeCertificateStructural( certificate );
        if ( normalized.check == Check::Approve )
        {
            auto slot = GetSlotKey( normalized.certificate.proposal() );
            auto winner = GetSubjectHash( normalized.certificate.proposal().subject() );
            if ( slot && winner )
            {
                auto slot_record = ReadCertificatePreflightRecord(
                    { std::string( CERTIFICATE_SLOT_BASE_PATH_KEY ) + slot.value() } );
                auto index_record = ReadCertificatePreflightRecord(
                    { std::string( CERTIFICATE_TX_INDEX_BASE_PATH_KEY ) + winner.value() } );
                if ( !slot_record ) return outcome::failure( slot_record.error() );
                if ( !index_record ) return outcome::failure( index_record.error() );
            }
        }
        const auto finalized = FinalizeSlot( certificate, DeliverySource::Local );
        if ( finalized == FinalizeResult::Conflict )
            return outcome::failure( CertificateStoreError::Conflict );
        if ( finalized == FinalizeResult::Invalid )
            return outcome::failure( CertificateStoreError::InvalidCertificate );
        if ( finalized == FinalizeResult::StorageFailure )
            return outcome::failure( CertificateStoreError::StorageError );
        return outcome::success();
    }

    bool ConsensusManager::RecordCertificateConflict( const CertificateNormalization &authoritative,
                                                       const CertificateNormalization &incoming,
                                                       const std::string              &slot_id,
                                                       DeliverySource                  source,
                                                       uint64_t                        now_ms )
    {
        if ( !state_store_ || authoritative.check != Check::Approve || incoming.check != Check::Approve ||
             authoritative.deterministic_bytes == incoming.deterministic_bytes || !IsCanonicalHash( slot_id ) ||
             now_ms == 0 )
            return false;

        const auto digest_of = []( const std::string &bytes )
        {
            const auto digest = crypto::sha2_256( bytes.data(), bytes.size() );
            return base::hex_lower( gsl::span<const uint8_t>( digest.data(), digest.size() ) );
        };
        const auto authoritative_digest = digest_of( authoritative.deterministic_bytes );
        const auto incoming_digest = digest_of( incoming.deterministic_bytes );
        if ( authoritative_digest == incoming_digest ) return false;

        const uint32_t source_bit = 1u << static_cast<uint8_t>( source );
        ConsensusStateStore::ConflictRecord conflict;
        conflict.set_schema_version( 2 );
        conflict.set_slot_id( slot_id );
        conflict.set_low_certificate_digest( authoritative_digest );
        conflict.set_low_proposal_id( authoritative.certificate.proposal_id() );
        conflict.set_high_certificate_digest( incoming_digest );
        conflict.set_high_proposal_id( incoming.certificate.proposal_id() );
        conflict.set_sources_bitset( source_bit );
        conflict.set_first_source( source_bit );
        conflict.set_first_seen_at_ms( now_ms );
        conflict.set_last_seen_at_ms( now_ms );
        conflict.set_observation_count( 1 );
        conflict.set_authoritative_certificate_digest( authoritative_digest );
        conflict.set_authoritative_proposal_id( authoritative.certificate.proposal_id() );
        conflict.set_incoming_certificate_digest( incoming_digest );
        conflict.set_incoming_proposal_id( incoming.certificate.proposal_id() );

        ConsensusStateStore::SafetyRecord safety;
        safety.set_schema_version( 2 );
        safety.set_state( ConsensusStateStore::SafetyRecord::SAFETY_VIOLATION );
        safety.set_slot_id( slot_id );
        safety.set_authoritative_certificate_digest( authoritative_digest );
        safety.set_authoritative_proposal_id( authoritative.certificate.proposal_id() );
        safety.set_updated_at_ms( now_ms );

        auto stored = state_store_->RecordConflictAndSafety( std::move( conflict ), std::move( safety ) );
        if ( !stored ) return false;
        const bool unique_pair = stored.value().observation_count() == 1;
        if ( unique_pair ) certificate_conflict_unique_pairs_.fetch_add( 1, std::memory_order_relaxed );
        {
            std::lock_guard lock( proposals_mutex_ );
            restored_safety_slots_.insert( slot_id );
            restored_safety_proposal_ids_.insert( stored.value().authoritative_proposal_id() );
            restored_safety_proposal_ids_.insert( stored.value().incoming_proposal_id() );
            slot_states_[slot_id].lifecycle = SlotState::Lifecycle::SafetyViolation;
            slot_cv_.notify_all();
        }
        const auto evidence_key = ConsensusStateStore::ConflictKey(
            slot_id, stored.value().low_certificate_digest(), stored.value().high_certificate_digest() );
        ConsensusManagerLogger()->critical(
            "certificate_safety_violation evidence_key={} slot={} authoritative_proposal_id={} "
            "incoming_proposal_id={} authoritative_digest={} incoming_digest={} first_source={} "
            "sources_bitset={} observation_count={} unique_pair={}",
            evidence_key,
            slot_id,
            stored.value().authoritative_proposal_id(),
            stored.value().incoming_proposal_id(),
            stored.value().authoritative_certificate_digest(),
            stored.value().incoming_certificate_digest(),
            stored.value().first_source(),
            stored.value().sources_bitset(),
            stored.value().observation_count(),
            unique_pair );
        if ( certificate_conflict_observer_ ) certificate_conflict_observer_( stored.value(), unique_pair );
        return true;
    }

    ConsensusManager::FinalizeResult ConsensusManager::FinalizeSlot( const Certificate &certificate,
                                                                      DeliverySource      source )
    {
        auto activity = BeginActivity();
        if ( !activity ) return FinalizeResult::StorageFailure;
        auto normalized = NormalizeCertificateStructural( certificate );
        if ( normalized.check != Check::Approve ) return FinalizeResult::Invalid;

        auto slot_result = GetSlotKey( normalized.certificate.proposal() );
        auto winner_result = GetSubjectHash( normalized.certificate.proposal().subject() );
        if ( !slot_result || !winner_result || !IsCanonicalHash( slot_result.value() ) ||
             !IsCanonicalHash( winner_result.value() ) )
            return FinalizeResult::Invalid;

        const auto &slot_id = slot_result.value();
        const auto &winner_id = winner_result.value();
        const auto digest_bytes = crypto::sha2_256( normalized.deterministic_bytes.data(),
                                                    normalized.deterministic_bytes.size() );
        const auto digest = base::hex_lower(
            gsl::span<const uint8_t>( digest_bytes.data(), digest_bytes.size() ) );

        const auto slot_key_string = std::string( CERTIFICATE_SLOT_BASE_PATH_KEY ) + slot_id;
        const auto index_key_string = std::string( CERTIFICATE_TX_INDEX_BASE_PATH_KEY ) + winner_id;
        auto initial_slot = ReadCertificatePreflightRecord( { slot_key_string } );
        auto initial_index = ReadCertificatePreflightRecord( { index_key_string } );
        if ( !initial_slot || !initial_index ) return FinalizeResult::StorageFailure;
        bool authority_existed = initial_slot.value().has_value();
        if ( authority_existed )
        {
            const auto authoritative = GetCertificateBySlotId( slot_id );
            if ( !authoritative ) return FinalizeResult::StorageFailure;
            const auto authoritative_normalized = NormalizeCertificateStructural( authoritative.value() );
            if ( authoritative_normalized.check != Check::Approve ) return FinalizeResult::StorageFailure;
            if ( authoritative_normalized.deterministic_bytes != normalized.deterministic_bytes )
            {
                if ( !RecordCertificateConflict(
                         authoritative_normalized, normalized, slot_id, source, CurrentTimeMs() ) )
                    return FinalizeResult::StorageFailure;
                return FinalizeResult::Conflict;
            }
            if ( !initial_index.value() || std::string( initial_index.value()->toString() ) != slot_id )
                return FinalizeResult::StorageFailure;
        }
        else if ( initial_index.value() )
        {
            return FinalizeResult::Conflict;
        }
        else if ( ValidateCertificateForFirstObservation( normalized, CurrentTimeMs() ) != Check::Approve )
        {
            return FinalizeResult::Invalid;
        }

        uint64_t reservation_generation = 0;
        uint64_t prior_generation = 0;
        SlotState::Lifecycle prior_lifecycle = SlotState::Lifecycle::Empty;
        if ( !authority_existed )
        {
            for ( ;; )
            {
                std::unique_lock lock( proposals_mutex_ );
                auto &slot = slot_states_[slot_id];
                while ( resource_admissions_inflight_.count( slot_id ) != 0 ||
                        slot.lifecycle == SlotState::Lifecycle::SigningPublishing ||
                        slot.lifecycle == SlotState::Lifecycle::PublishingReplay ||
                        slot.lifecycle == SlotState::Lifecycle::Finalizing )
                {
                    if ( stop_timer_.load() ) return FinalizeResult::StorageFailure;
                    if ( finalization_stage_observer_ ) finalization_stage_observer_( "waiting-publication" );
                    slot_cv_.wait(
                        lock,
                        [this, &slot, &slot_id]()
                        {
                            return stop_timer_.load() ||
                                   ( resource_admissions_inflight_.count( slot_id ) == 0 &&
                                     slot.lifecycle != SlotState::Lifecycle::SigningPublishing &&
                                     slot.lifecycle != SlotState::Lifecycle::PublishingReplay &&
                                     slot.lifecycle != SlotState::Lifecycle::Finalizing );
                        } );
                }
                if ( stop_timer_.load() ) return FinalizeResult::StorageFailure;

                lock.unlock();
                auto authority = GetCertificateBySlotId( slot_id );
                if ( authority )
                {
                    if ( authority.value().SerializeAsString() != normalized.deterministic_bytes )
                    {
                        const auto authoritative_normalized = NormalizeCertificateStructural( authority.value() );
                        if ( authoritative_normalized.check != Check::Approve ||
                             !RecordCertificateConflict(
                                 authoritative_normalized, normalized, slot_id, source, CurrentTimeMs() ) )
                            return FinalizeResult::StorageFailure;
                        return FinalizeResult::Conflict;
                    }
                    authority_existed = true;
                    break;
                }
                if ( authority.error() != make_error_code( CertificateStoreError::NotFound ) )
                    return FinalizeResult::StorageFailure;

                lock.lock();
                auto &rechecked = slot_states_[slot_id];
                if ( rechecked.lifecycle == SlotState::Lifecycle::SigningPublishing ||
                     rechecked.lifecycle == SlotState::Lifecycle::PublishingReplay ||
                     rechecked.lifecycle == SlotState::Lifecycle::Finalizing )
                    continue;
                prior_lifecycle = rechecked.lifecycle;
                prior_generation = rechecked.generation;
                reservation_generation = ++rechecked.generation;
                rechecked.lifecycle = SlotState::Lifecycle::Finalizing;
                rechecked.reserved_finalization_proposal_id = normalized.certificate.proposal_id();
                rechecked.reserved_finalization_digest = digest;
                rechecked.reserved_finalization_winner_id = winner_id;
                break;
            }
        }
        else
        {
            std::unique_lock lock( proposals_mutex_ );
            auto &slot = slot_states_[slot_id];
            slot_cv_.wait(
                lock,
                [this, &slot, &slot_id]()
                {
                    return stop_timer_.load() ||
                           ( resource_admissions_inflight_.count( slot_id ) == 0 &&
                             slot.lifecycle != SlotState::Lifecycle::SigningPublishing &&
                             slot.lifecycle != SlotState::Lifecycle::PublishingReplay &&
                             slot.lifecycle != SlotState::Lifecycle::Finalizing &&
                             slot.lifecycle != SlotState::Lifecycle::Reconciling );
                } );
            if ( stop_timer_.load() ) return FinalizeResult::StorageFailure;
            // A recorded safety violation is terminal for participation, but
            // the original authoritative winner may still retry application.
            // Preserve that lifecycle while allowing the exact retry through.
            if ( slot.lifecycle != SlotState::Lifecycle::SafetyViolation )
            {
                prior_lifecycle = slot.lifecycle;
                prior_generation = slot.generation;
                reservation_generation = ++slot.generation;
                slot.lifecycle = SlotState::Lifecycle::Finalizing;
                slot.reserved_finalization_proposal_id = normalized.certificate.proposal_id();
                slot.reserved_finalization_digest = digest;
                slot.reserved_finalization_winner_id = winner_id;
            }
        }

        if ( reservation_generation != 0 && finalization_stage_observer_ )
            finalization_stage_observer_( "reserved" );

        bool newly_persisted = false;
        if ( !authority_existed )
        {
            auto existing_slot = ReadCertificatePreflightRecord( { slot_key_string } );
            auto existing_index = ReadCertificatePreflightRecord( { index_key_string } );
            bool exact_pair = existing_slot && existing_index && existing_slot.value() && existing_index.value() &&
                              std::string( existing_slot.value()->toString() ) == normalized.deterministic_bytes &&
                              std::string( existing_index.value()->toString() ) == slot_id;
            bool confirmed_absent = existing_slot && existing_index && !existing_slot.value() && !existing_index.value();

            outcome::result<CID> put_result = outcome::failure( std::errc::io_error );
            if ( confirmed_absent )
            {
                crdt::GlobalDB::Buffer cert_value;
                cert_value.put( normalized.deterministic_bytes );
                crdt::GlobalDB::Buffer index_value;
                index_value.put( slot_id );
                put_result = db_->Put( { { crdt::HierarchicalKey( slot_key_string ), std::move( cert_value ) },
                                         { crdt::HierarchicalKey( index_key_string ), std::move( index_value ) } },
                                       { consensus_datastore_topic_ } );
                newly_persisted = put_result.has_value();
                exact_pair = newly_persisted;
            }

            auto reread = newly_persisted
                              ? outcome::result<Certificate>( normalized.certificate )
                              : GetCertificateBySlotId( slot_id );
            if ( reread )
            {
                if ( reread.value().SerializeAsString() != normalized.deterministic_bytes )
                {
                    const auto authoritative_normalized = NormalizeCertificateStructural( reread.value() );
                    if ( authoritative_normalized.check != Check::Approve ||
                         !RecordCertificateConflict(
                             authoritative_normalized, normalized, slot_id, source, CurrentTimeMs() ) )
                        return FinalizeResult::StorageFailure;
                    return FinalizeResult::Conflict;
                }
                exact_pair = true;
            }
            else if ( reread.error() == make_error_code( CertificateStoreError::NotFound ) &&
                      confirmed_absent && put_result.has_error() )
            {
                std::lock_guard lock( proposals_mutex_ );
                auto it = slot_states_.find( slot_id );
                if ( it != slot_states_.end() && it->second.lifecycle == SlotState::Lifecycle::Finalizing &&
                     it->second.generation == reservation_generation &&
                     it->second.reserved_finalization_proposal_id == normalized.certificate.proposal_id() &&
                     it->second.reserved_finalization_digest == digest &&
                     it->second.reserved_finalization_winner_id == winner_id )
                {
                    it->second.lifecycle = prior_lifecycle;
                    it->second.generation = prior_generation;
                    it->second.reserved_finalization_proposal_id.clear();
                    it->second.reserved_finalization_digest.clear();
                    it->second.reserved_finalization_winner_id.clear();
                    slot_cv_.notify_all();
                }
                return FinalizeResult::StorageFailure;
            }
            if ( !exact_pair ) return FinalizeResult::StorageFailure;
        }

        bool first_local_finality = false;
        {
            std::lock_guard lock( proposals_mutex_ );
            auto &slot = slot_states_[slot_id];
            first_local_finality = restored_final_slots_.insert( slot_id ).second;
            if ( slot.lifecycle != SlotState::Lifecycle::SafetyViolation )
                slot.lifecycle = SlotState::Lifecycle::FinalizedPendingApplication;
            slot.reserved_finalization_proposal_id = normalized.certificate.proposal_id();
            slot.reserved_finalization_digest = digest;
            slot.reserved_finalization_winner_id = winner_id;
            slot_cv_.notify_all();
        }

        EmitConsensusTrace( { ConsensusTraceEvent::Stage::AuthorityEstablished,
                              account_address_,
                              slot_id,
                              normalized.certificate.proposal_id(),
                              winner_id,
                              digest,
                              source } );

        if ( normalized.certificate.proposal().subject().has_subject_type_hash() &&
             SubjectTypeMatches( normalized.certificate.proposal().subject(), NONCE_SUBJECT_TYPE ) )
        {
            auto nonce = DecodeNonceSubject( normalized.certificate.proposal().subject() );
            if ( !nonce ) return FinalizeResult::StorageFailure;
            if ( nonce.value().transaction().has_mint_v2() )
            {
                auto outpoint = DecodeMintBurnOutpoint( normalized.certificate.proposal().subject() );
                if ( !outpoint ) return FinalizeResult::StorageFailure;
                auto expected_slot = MintSlotForOutpoint( outpoint.value() );
                if ( !expected_slot || expected_slot.value() != slot_id || !state_store_ )
                    return FinalizeResult::StorageFailure;
                auto finalized_burn = state_store_->FinalizeBurnReservation(
                    slot_id, outpoint.value(), digest, normalized.certificate.proposal_id(), winner_id,
                    CurrentTimeMs() );
                if ( !finalized_burn ) return FinalizeResult::StorageFailure;
                if ( finalization_stage_observer_ ) finalization_stage_observer_( "burn-finalized" );
            }
        }

        ConsensusStateStore::ProcessRecord pending;
        pending.set_schema_version( 2 );
        pending.set_state( ConsensusStateStore::ProcessRecord::PENDING );
        pending.set_slot_id( slot_id );
        pending.set_certificate_digest( digest );
        pending.set_proposal_id( normalized.certificate.proposal_id() );
        pending.set_winner_id( winner_id );
        pending.set_updated_at_ms( CurrentTimeMs() );
        if ( !state_store_ || !state_store_->PutPendingProcess( pending ) )
            return FinalizeResult::StorageFailure;
        auto process = state_store_->GetProcess( slot_id );
        if ( !process || !process.value() ) return FinalizeResult::StorageFailure;
        {
            std::lock_guard lock( restored_state_mutex_ );
            restored_processes_[slot_id] = process.value().value();
        }

        if ( first_local_finality ) registry_->OnFinalizedCertificate( normalized.certificate );
        if ( source == DeliverySource::Local && newly_persisted )
        {
            ConsensusMessage message;
            *message.mutable_certificate() = normalized.certificate;
            if ( certificate_publish_observer_ ) certificate_publish_observer_();
            (void) Publish( message );
        }

        auto processed = ProcessFinalizedCertificate( normalized, slot_id, winner_id );
        if ( processed == FinalizeResult::Applied || processed == FinalizeResult::AlreadyFinalized )
            (void) certificate_work_journal_->MarkDone(
                std::string( CERTIFICATE_SLOT_BASE_PATH_KEY ) + slot_id );
        if ( processed == FinalizeResult::PendingApplication || processed == FinalizeResult::StorageFailure )
            return processed;
        return authority_existed && processed == FinalizeResult::Applied
                   ? FinalizeResult::AlreadyFinalized
                   : processed;
    }

    ConsensusManager::FinalizeResult ConsensusManager::ProcessFinalizedCertificate(
        const CertificateNormalization &normalized,
        const std::string              &slot_id,
        const std::string              &winner_id )
    {
        auto process = state_store_->GetProcess( slot_id );
        if ( !process || !process.value() ) return FinalizeResult::StorageFailure;
        auto record = process.value().value();
        const auto digest_bytes = crypto::sha2_256( normalized.deterministic_bytes.data(),
                                                    normalized.deterministic_bytes.size() );
        const auto digest = base::hex_lower(
            gsl::span<const uint8_t>( digest_bytes.data(), digest_bytes.size() ) );
        if ( record.proposal_id() != normalized.certificate.proposal_id() ||
             record.winner_id() != winner_id || record.certificate_digest() != digest )
            return FinalizeResult::StorageFailure;

        std::optional<ConsensusStateStore::BurnReservationRecord> burn_reservation;
        std::optional<ConsensusStateStore::FinalizedReservationIdentity> expected_burn_identity;
        const auto terminal_identity_matches = [&]( const ConsensusStateStore::BurnReservationRecord &current )
        {
            return expected_burn_identity &&
                   current.slot_id() == expected_burn_identity->slot_id &&
                   current.source_chain() == expected_burn_identity->outpoint.source_chain &&
                   current.burn_hash() == expected_burn_identity->outpoint.burn_hash &&
                   current.receipt_log_index() == expected_burn_identity->outpoint.receipt_log_index &&
                   current.generation() == expected_burn_identity->generation &&
                   current.certificate_digest() == expected_burn_identity->certificate_digest &&
                   current.proposal_id() == expected_burn_identity->proposal_id &&
                   current.winner_id() == expected_burn_identity->winner_id;
        };
        const auto reject_terminal_identity_mismatch = [&]( const auto &current )
        {
            ConsensusManagerLogger()->critical(
                "burn_terminal_identity_mismatch slot={} terminal_state={} "
                "expected_outpoint={}:{}:{} expected_generation={} expected_certificate_digest={} "
                "expected_proposal_id={} expected_winner_id={} observed_outpoint={}:{}:{} "
                "observed_generation={} observed_certificate_digest={} observed_proposal_id={} "
                "observed_winner_id={}",
                slot_id, static_cast<int>( current.state() ),
                expected_burn_identity->outpoint.source_chain,
                expected_burn_identity->outpoint.burn_hash,
                expected_burn_identity->outpoint.receipt_log_index,
                expected_burn_identity->generation,
                expected_burn_identity->certificate_digest,
                expected_burn_identity->proposal_id,
                expected_burn_identity->winner_id,
                current.source_chain(), current.burn_hash(), current.receipt_log_index(),
                current.generation(), current.certificate_digest(), current.proposal_id(),
                current.winner_id() );
        };
        if ( normalized.certificate.proposal().subject().has_subject_type_hash() &&
             SubjectTypeMatches( normalized.certificate.proposal().subject(), NONCE_SUBJECT_TYPE ) )
        {
            auto nonce = DecodeNonceSubject( normalized.certificate.proposal().subject() );
            if ( !nonce ) return FinalizeResult::StorageFailure;
            if ( nonce.value().transaction().has_mint_v2() )
            {
                auto outpoint = DecodeMintBurnOutpoint( normalized.certificate.proposal().subject() );
                if ( !outpoint ) return FinalizeResult::StorageFailure;
                auto reservation = state_store_->GetBurnReservation( slot_id );
                if ( !reservation || !reservation.value() ) return FinalizeResult::StorageFailure;
                burn_reservation = reservation.value().value();
                expected_burn_identity = ConsensusStateStore::FinalizedReservationIdentity{
                    slot_id,
                    outpoint.value(),
                    burn_reservation->generation(),
                    digest,
                    record.proposal_id(),
                    record.winner_id() };
                if ( burn_reservation->state() == ConsensusStateStore::BurnReservationRecord::SAFETY_ERROR ||
                     burn_reservation->state() ==
                         ConsensusStateStore::BurnReservationRecord::CONSUMED_SAFETY_ERROR )
                {
                    if ( !terminal_identity_matches( *burn_reservation ) )
                    {
                        reject_terminal_identity_mismatch( *burn_reservation );
                        return FinalizeResult::StorageFailure;
                    }
                    std::lock_guard lock( proposals_mutex_ );
                    slot_states_[slot_id].lifecycle = SlotState::Lifecycle::SafetyViolation;
                    return FinalizeResult::AlreadyFinalized;
                }
            }
        }
        if ( record.state() == ConsensusStateStore::ProcessRecord::COMPLETE )
        {
            {
                std::lock_guard lock( proposals_mutex_ );
                auto &slot = slot_states_[slot_id];
                if ( slot.lifecycle != SlotState::Lifecycle::SafetyViolation )
                    slot.lifecycle = SlotState::Lifecycle::Applied;
            }
            ClearProposalSlot( normalized.certificate.proposal() );
            return FinalizeResult::AlreadyFinalized;
        }

        CertificateSubjectHandler handler;
        CertificateApplicationHandler application_handler;
        {
            std::shared_lock lock( certificate_handlers_mutex_ );
            const auto &type_hash = normalized.certificate.proposal().subject().subject_type_hash().hash();
            auto typed = certificate_application_handlers_.find( type_hash );
            if ( typed != certificate_application_handlers_.end() ) application_handler = typed->second;
            auto legacy = certificate_subject_handlers_.find( type_hash );
            if ( legacy != certificate_subject_handlers_.end() ) handler = legacy->second;
            if ( !application_handler && !handler ) return FinalizeResult::PendingApplication;
        }
        {
            std::lock_guard lock( restored_state_mutex_ );
            if ( !processing_slots_.insert( slot_id ).second ) return FinalizeResult::PendingApplication;
        }
        const auto release_processing = [this, &slot_id]()
        {
            std::lock_guard lock( restored_state_mutex_ );
            processing_slots_.erase( slot_id );
        };

        const auto now = CurrentTimeMs();
        if ( record.state() == ConsensusStateStore::ProcessRecord::PROCESSING )
            (void) state_store_->RestorePending( slot_id, now );
        if ( !state_store_->MarkProcessing( slot_id, now + 15'000, now ) )
        {
            release_processing();
            return FinalizeResult::StorageFailure;
        }

        ApplicationDisposition disposition = ApplicationDisposition::Retryable;
        if ( application_handler && burn_reservation )
        {
            auto handled = application_handler(
                winner_id, normalized.certificate,
                FinalizedReservationApplicationHandle{ state_store_, *expected_burn_identity } );
            if ( handled ) disposition = handled.value();
        }
        else
        {
            auto handled = handler( winner_id, normalized.certificate );
            if ( handled && handled.value() == Check::Approve ) disposition = ApplicationDisposition::Applied;
        }
        const auto restore_pending = [&]()
        {
            (void) state_store_->RestorePending( slot_id, CurrentTimeMs() );
            auto pending = state_store_->GetProcess( slot_id );
            if ( pending && pending.value() )
            {
                std::lock_guard lock( restored_state_mutex_ );
                restored_processes_[slot_id] = pending.value().value();
            }
            release_processing();
        };
        if ( expected_burn_identity &&
             ( disposition == ApplicationDisposition::Applied ||
               disposition == ApplicationDisposition::AlreadyApplied ) )
        {
            auto durable = state_store_->GetBurnReservation( expected_burn_identity->slot_id );
            if ( !durable || !durable.value() )
            {
                restore_pending();
                return FinalizeResult::PendingApplication;
            }
            const auto &current = durable.value().value();
            const bool exact_identity = terminal_identity_matches( current );
            if ( exact_identity &&
                 current.state() == ConsensusStateStore::BurnReservationRecord::FINALIZED_PENDING_APPLICATION )
            {
                restore_pending();
                return FinalizeResult::PendingApplication;
            }
            if ( exact_identity &&
                 ( current.state() == ConsensusStateStore::BurnReservationRecord::SAFETY_ERROR ||
                   current.state() ==
                       ConsensusStateStore::BurnReservationRecord::CONSUMED_SAFETY_ERROR ) )
            {
                {
                    std::lock_guard lock( proposals_mutex_ );
                    slot_states_[slot_id].lifecycle = SlotState::Lifecycle::SafetyViolation;
                }
                release_processing();
                return FinalizeResult::AlreadyFinalized;
            }
            if ( !exact_identity &&
                 ( current.state() == ConsensusStateStore::BurnReservationRecord::SAFETY_ERROR ||
                   current.state() ==
                       ConsensusStateStore::BurnReservationRecord::CONSUMED_SAFETY_ERROR ) )
            {
                reject_terminal_identity_mismatch( current );
                restore_pending();
                return FinalizeResult::StorageFailure;
            }
            if ( !exact_identity ||
                 current.state() != ConsensusStateStore::BurnReservationRecord::CONSUMED )
                disposition = ApplicationDisposition::Irreconcilable;
        }
        if ( disposition == ApplicationDisposition::Irreconcilable )
        {
            if ( !burn_reservation )
            {
                (void) state_store_->RestorePending( slot_id, CurrentTimeMs() );
                release_processing();
                return FinalizeResult::StorageFailure;
            }
            auto safety = state_store_->MarkBurnReservationSafetyError(
                slot_id, burn_reservation->generation(), digest, normalized.certificate.proposal_id(), winner_id,
                "irreconcilable exact-winner application state", CurrentTimeMs() );
            release_processing();
            if ( !safety ) return FinalizeResult::StorageFailure;
            {
                std::lock_guard lock( proposals_mutex_ );
                slot_states_[slot_id].lifecycle = SlotState::Lifecycle::SafetyViolation;
            }
            ConsensusManagerLogger()->critical(
                "burn_application_safety_error slot={} proposal_id={} winner_id={} certificate_digest={}",
                slot_id, normalized.certificate.proposal_id(), winner_id, digest );
            return FinalizeResult::AlreadyFinalized;
        }
        if ( disposition == ApplicationDisposition::Retryable )
        {
            restore_pending();
            return FinalizeResult::PendingApplication;
        }
        if ( !state_store_->MarkComplete( slot_id, CurrentTimeMs() ) )
        {
            (void) state_store_->RestorePending( slot_id, CurrentTimeMs() );
            release_processing();
            return FinalizeResult::StorageFailure;
        }
        auto complete = state_store_->GetProcess( slot_id );
        if ( complete && complete.value() )
        {
            std::lock_guard lock( restored_state_mutex_ );
            restored_processes_[slot_id] = complete.value().value();
        }
        {
            std::lock_guard lock( proposals_mutex_ );
            auto &slot = slot_states_[slot_id];
            if ( slot.lifecycle != SlotState::Lifecycle::SafetyViolation )
                slot.lifecycle = SlotState::Lifecycle::Applied;
        }
        release_processing();
        ClearProposalSlot( normalized.certificate.proposal() );
        (void) WakePendingDependency( PendingDependencyKey::Certificate( winner_id ) );
        return FinalizeResult::Applied;
    }

    void ConsensusManager::HandleProposal( const Proposal &proposal )
    {
        if ( !CheckProposal( proposal ) )
        {
            ConsensusManagerLogger()->error( "{}: rejected: Invalid proposal for hash {} proposal_id={}",
                                             __func__,
                                             GetPrintableSubjectHash( proposal.subject() ),
                                             proposal.proposal_id().substr( 0, 8 ) );
            return;
        }

        const auto &subject     = proposal.subject();
        const auto &proposal_id = proposal.proposal_id();
        if ( !IsTimestampSane( proposal.timestamp() ) )
        {
            ConsensusManagerLogger()->error( "{}: rejected: timestamp out of bounds for hash {} proposal_id={}",
                                             __func__,
                                             GetPrintableSubjectHash( subject ),
                                             proposal_id.substr( 0, 8 ) );
            return;
        }

        if ( proposal.registry_cid().empty() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: proposal registry CID missing for hash {}. proposal_id={}",
                                             __func__,
                                             GetPrintableSubjectHash( proposal.subject() ),
                                             proposal.proposal_id().substr( 0, 8 ) );
            return;
        }

        auto subject_hash_result = GetSubjectHash( subject );
        if ( subject_hash_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: subject hash missing proposal_id={}",
                                             __func__,
                                             proposal_id.substr( 0, 8 ) );
            return;
        }
        const auto &subject_hash = subject_hash_result.value();

        auto slot_result = GetSlotKey( proposal );
        if ( slot_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: canonical slot derivation failed proposal_id={}",
                                             __func__,
                                             proposal.proposal_id().substr( 0, 8 ) );
            return;
        }
        const auto &slot_key = slot_result.value();

        {
            std::lock_guard lock( proposals_mutex_ );
            if ( restored_safety_slots_.count( slot_key ) != 0 )
            {
                ConsensusManagerLogger()->critical(
                    "{}: proposal admission blocked by slot safety violation slot={} proposal_id={}",
                    __func__, slot_key, proposal.proposal_id() );
                return;
            }
        }

        auto proposal_registry_result = registry_->LoadRegistryByCid( proposal.registry_cid() );
        if ( proposal_registry_result.has_error() )
        {
            ConsensusManagerLogger()->warn(
                "{}: deferred: registry load error={} proposal={} proposal_id={} hash={}. Keeping proposal pending",
                __func__,
                proposal_registry_result.error().message(),
                proposal.registry_cid(),
                proposal_id.substr( 0, 8 ),
                subject_hash.substr( 0, 8 ) );
            AddPendingProposal( proposal, subject_hash );
            return;
        }
        if ( proposal.registry_epoch() != proposal_registry_result.value().epoch() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: registry epoch mismatch proposal={} registry={}",
                                             __func__,
                                             proposal.registry_epoch(),
                                             proposal_registry_result.value().epoch() );
            return;
        }

        if ( !CheckSubject( subject ) )
        {
            return;
        }

        if ( CheckCertificateForSubject( subject_hash ) )
        {
            ConsensusManagerLogger()->debug( "{}: ignored: subject already certified hash={} proposal_id={}",
                                             __func__,
                                             subject_hash.substr( 0, 8 ),
                                             proposal_id.substr( 0, 8 ) );
            std::lock_guard lock( proposals_mutex_ );
            RemovePendingProposalLocked( proposal_id, "already-certified" );
            return;
        }

        const auto     type_hash = ParseSubjectTypeHash( subject ).value();
        SubjectHandler subject_handler;
        {
            std::shared_lock lock( subject_handlers_mutex_ );
            auto             handler_it = subject_handlers_.find( type_hash );
            if ( handler_it == subject_handlers_.end() )
            {
                ConsensusManagerLogger()->error(
                    "{}: rejected: subject handler missing type_hash={}",
                    __func__,
                    base::hex_lower( gsl::span<const uint8_t>(
                        reinterpret_cast<const uint8_t *>( subject.subject_type_hash().hash().data() ),
                        subject.subject_type_hash().hash().size() ) ) );
                return;
            }
            subject_handler = handler_it->second;
        }

        auto subject_result = subject_handler( subject );
        if ( subject_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: subject handler error for hash {} proposal_id={}",
                                             __func__,
                                             subject_hash.substr( 0, 8 ),
                                             proposal_id.substr( 0, 8 ) );
            return;
        }

        const auto &validation_result = subject_result.value();
        switch ( validation_result.check )
        {
            case Check::Reject:
                ConsensusManagerLogger()->error( "{}: rejected: subject check failed for hash {} proposal_id={}",
                                                 __func__,
                                                 subject_hash.substr( 0, 8 ),
                                                 proposal_id.substr( 0, 8 ) );
                return;
            case Check::Stalled:
                ConsensusManagerLogger()->warn( "{}: stalled: subject handler stalled for hash {} proposal_id={}",
                                                __func__,
                                                subject_hash.substr( 0, 8 ),
                                                proposal_id.substr( 0, 8 ) );
                return;
            case Check::Pending:
                AddPendingProposal( proposal, subject_hash, validation_result );
                return;
            case Check::Approve:
                if ( !AdmitProposalResources( proposal, slot_key ) )
                {
                    return;
                }
                ContinueProposalAfterSubject( proposal, slot_key );
                return;
        }
    }

    outcome::result<void> ConsensusManager::ResumeProposalHandling( const std::string &subject_hash )
    {
        if ( subject_hash.empty() )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        ConsensusManagerLogger()->trace( "{}: Attempting to resume proposals for hash={}",
                                         __func__,
                                         subject_hash.substr( 0, 8 ) );

        for ( const auto &proposal : TakePendingProposals( subject_hash ) )
        {
            RetryPendingProposal( proposal, "subject-resume" );
        }
        return outcome::success();
    }

    void ConsensusManager::ProcessCertificates()
    {
        std::vector<ProposalState> to_process;
        {
            std::lock_guard lock( proposals_mutex_ );
            for ( const auto &[_, state] : proposals_ )
            {
                if ( state.quorum_reached )
                {
                    to_process.push_back( state );
                }
            }
        }

        const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch() )
                                .count();
        for ( const auto &state : to_process )
        {
            const auto &proposal     = state.proposal;
            const auto &proposal_id  = proposal.proposal_id();
            {
                std::lock_guard lock( proposals_mutex_ );
                if ( restored_safety_slots_.count( state.slot_key ) != 0 )
                {
                    continue;
                }
            }
            auto subject_hash = GetSubjectHash( proposal.subject() );
            if ( subject_hash.has_value() && CheckCertificateForSubject( subject_hash.value() ) )
            {
                ConsensusManagerLogger()->debug( "{}: hash {} already certified, finalizing proposal_id={}",
                                                 __func__,
                                                 subject_hash.value().substr( 0, 8 ),
                                                 proposal_id.substr( 0, 8 ) );
                auto authoritative = GetCertificateBySubjectHash( subject_hash.value() );
                if ( authoritative )
                {
                    (void) FinalizeSlot( authoritative.value(), DeliverySource::Recovery );
                }
                continue;
            }

            const bool certificate_delay_elapsed = state.quorum_reached_ts_ms == 0 ||
                                                   std::chrono::milliseconds(
                                                       now_ms - static_cast<int64_t>( state.quorum_reached_ts_ms ) ) >=
                                                       certificate_delay_;
            if ( !certificate_delay_elapsed )
            {
                continue;
            }

            const auto round = GetCurrentRound( proposal.timestamp() );
            if ( round == state.last_attempt_round )
            {
                ConsensusManagerLogger()->debug(
                    "{}: proposal already attempted in round for hash {} proposal_id={} round={}",
                    __func__,
                    GetPrintableSubjectHash( proposal.subject() ),
                    proposal_id.substr( 0, 8 ),
                    round );
                continue;
            }

            auto proposal_registry_result = registry_->LoadRegistryByCid( proposal.registry_cid() );
            if ( proposal_registry_result.has_error() )
            {
                ConsensusManagerLogger()->debug( "{}: skipping proposal due to registry load error={} proposal_id={}",
                                                 __func__,
                                                 proposal_registry_result.error().message(),
                                                 proposal_id.substr( 0, 8 ) );
                continue;
            }
            const auto &proposal_registry = proposal_registry_result.value();
            if ( proposal.registry_epoch() != proposal_registry.epoch() )
            {
                ConsensusManagerLogger()->debug( "{}: skipping proposal due to registry epoch mismatch proposal_id={}",
                                                 __func__,
                                                 proposal_id.substr( 0, 8 ) );
                continue;
            }

            const auto aggregator_role = GetAggregatorRole( proposal, proposal_registry );
            if ( aggregator_role == AggregatorRole::NotInRegistry )
            {
                ConsensusManagerLogger()->debug(
                    "{}: local node not in proposal registry; retaining local proposal for hash {} proposal_id={}",
                    __func__,
                    GetPrintableSubjectHash( proposal.subject() ),
                    proposal_id.substr( 0, 8 ) );
                continue;
            }
            if ( aggregator_role == AggregatorRole::ActiveButNotAggregator )
            {
                ConsensusManagerLogger()->debug( "{}: not aggregator for proposal for hash {} proposal_id={}",
                                                 __func__,
                                                 GetPrintableSubjectHash( proposal.subject() ),
                                                 proposal_id.substr( 0, 8 ) );
                continue;
            }

            {
                std::lock_guard lock( proposals_mutex_ );
                auto            it = proposals_.find( proposal_id );
                if ( it != proposals_.end() )
                {
                    it->second.last_attempt_round = round;
                }
            }
            ConsensusManagerLogger()->debug( "{}: Attempting to create certificate for hash {} proposal_id={} round={}",
                                             __func__,
                                             GetPrintableSubjectHash( proposal.subject() ),
                                             proposal_id.substr( 0, 8 ),
                                             round );
            auto certificate_result = CreateCertificate( proposal, state.votes );
            if ( certificate_result.has_error() )
            {
                ConsensusManagerLogger()->error(
                    "{}: failed: certificate creation error for hash {} proposal_id {}: {}",
                    __func__,
                    GetPrintableSubjectHash( proposal.subject() ),
                    proposal_id.substr( 0, 8 ),
                    certificate_result.error().message() );
                continue;
            }

            if ( SubmitCertificate( certificate_result.value() ).has_error() )
            {
                continue;
            }
            ConsensusManagerLogger()->debug( "{}: certificate submitted for hash {} proposal_id={}",
                                             __func__,
                                             GetPrintableSubjectHash( proposal.subject() ),
                                             proposal_id.substr( 0, 8 ) );
        }
    }

    bool ConsensusManager::RegisterCertificateFilter()
    {
        auto weak_self = weak_from_this();
        certificate_delta_filter_registered_ = db_->RegisterDeltaFilter(
            std::string( CERT_NAMESPACE_KEY_PATTERN ),
            [weak_self]( const crdt::pb::Delta &delta )
            {
                if ( auto strong = weak_self.lock() )
                {
                    auto activity = strong->BeginActivity();
                    if ( !activity ) return crdt::DeltaFilterResult::Reject();
                    return strong->FilterCertificateDelta( delta );
                }
                return crdt::DeltaFilterResult::Reject();
            } );

        certificate_filter_registered_ = db_->RegisterElementFilter(
            std::string( CERT_SLOT_KEY_PATTERN ),
            [weak_self]( const crdt::pb::Element &element ) -> std::optional<std::vector<crdt::pb::Element>>
            {
                if ( auto strong = weak_self.lock() )
                {
                    auto activity = strong->BeginActivity();
                    if ( !activity ) return std::nullopt;
                    return strong->FilterCertificate( element );
                }
                return std::nullopt;
            } );

        certificate_callback_registered_ = db_->RegisterNewElementCallback(
            std::string( CERT_SLOT_KEY_PATTERN ),
            [weak_self]( crdt::CRDTCallbackManager::NewDataPair new_data, const std::string &cid )
            {
                if ( auto strong = weak_self.lock() )
                {
                    auto activity = strong->BeginActivity();
                    if ( !activity ) return;
                    strong->CertificateReceived( std::move( new_data ), cid );
                }
            } );

        db_->AddListenTopic( consensus_datastore_topic_ );

        return certificate_delta_filter_registered_ && certificate_filter_registered_ &&
               certificate_callback_registered_;
    }

    crdt::DeltaFilterResult ConsensusManager::FilterCertificateDelta( const crdt::pb::Delta &delta )
    {
        static const std::regex namespace_regex{ std::string( CERT_NAMESPACE_KEY_PATTERN ) };
        static const std::regex slot_regex{ std::string( CERT_SLOT_KEY_PATTERN ) };
        static const std::regex index_regex{ std::string( CERT_TX_INDEX_KEY_PATTERN ) };

        for ( const auto &tombstone : delta.tombstones() )
        {
            if ( std::regex_match( tombstone.key(), namespace_regex ) )
            {
                ConsensusManagerLogger()->critical( "{}: certificate tombstone is forbidden key={} id={}",
                                                    __func__,
                                                    tombstone.key(),
                                                    tombstone.id() );
                return crdt::DeltaFilterResult::Reject();
            }
        }

        const crdt::pb::Element *slot_element  = nullptr;
        const crdt::pb::Element *index_element = nullptr;
        std::size_t              certificate_count = 0;
        for ( const auto &element : delta.elements() )
        {
            if ( !std::regex_match( element.key(), namespace_regex ) )
            {
                continue;
            }
            ++certificate_count;
            if ( std::regex_match( element.key(), slot_regex ) )
            {
                if ( slot_element )
                {
                    ConsensusManagerLogger()->critical( "{}: duplicate slot record in certificate delta", __func__ );
                    return crdt::DeltaFilterResult::Reject();
                }
                slot_element = &element;
            }
            else if ( std::regex_match( element.key(), index_regex ) )
            {
                if ( index_element )
                {
                    ConsensusManagerLogger()->critical( "{}: duplicate index record in certificate delta", __func__ );
                    return crdt::DeltaFilterResult::Reject();
                }
                index_element = &element;
            }
            else
            {
                ConsensusManagerLogger()->critical( "{}: legacy or malformed certificate key={}",
                                                    __func__,
                                                    element.key() );
                return crdt::DeltaFilterResult::Reject();
            }
        }

        if ( certificate_count != 2 || !slot_element || !index_element )
        {
            ConsensusManagerLogger()->critical(
                "{}: rejected partial certificate delta elements={} has_slot={} has_index={}",
                __func__,
                certificate_count,
                slot_element != nullptr,
                index_element != nullptr );
            return crdt::DeltaFilterResult::Reject();
        }

        Certificate certificate;
        if ( !certificate.ParseFromString( slot_element->value() ) )
        {
            ConsensusManagerLogger()->critical( "{}: invalid certificate payload key={}",
                                                __func__,
                                                slot_element->key() );
            return crdt::DeltaFilterResult::Reject();
        }

        if ( HasUnknownFieldsRecursively( certificate ) || certificate.proposal_id().empty() ||
             !certificate.has_proposal() )
        {
            return crdt::DeltaFilterResult::Reject();
        }
        const auto &proposal = certificate.proposal();
        if ( proposal.proposal_id() != certificate.proposal_id() ||
             proposal.registry_cid() != certificate.registry_cid() ||
             proposal.registry_epoch() != certificate.registry_epoch() ||
             !ValidateSubject( proposal.subject() ) || !CheckProposal( proposal ) ||
             CreateProposalId( proposal ) != certificate.proposal_id() ||
             certificate.votes().empty() )
        {
            return crdt::DeltaFilterResult::Reject();
        }

        std::unordered_set<std::string> voters;
        uint64_t                        max_vote_timestamp = 0;
        const Vote                     *previous_vote = nullptr;
        for ( const auto &vote : certificate.votes() )
        {
            if ( vote.proposal_id() != certificate.proposal_id() || !CheckVote( vote ) ||
                 !voters.insert( vote.voter_id() ).second )
            {
                return crdt::DeltaFilterResult::Reject();
            }
            auto signing_bytes = VoteSigningBytes( vote );
            if ( signing_bytes.has_error() ||
                 !GeniusAccount::VerifySignature( vote.voter_id(), vote.signature(), signing_bytes.value() ) )
            {
                return crdt::DeltaFilterResult::Reject();
            }
            if ( previous_vote && VoterIdBytewiseLess( vote, *previous_vote ) )
            {
                return crdt::DeltaFilterResult::Reject();
            }
            previous_vote      = &vote;
            max_vote_timestamp = std::max( max_vote_timestamp, vote.timestamp() );
        }
        if ( max_vote_timestamp == 0 || certificate.timestamp() != max_vote_timestamp )
        {
            return crdt::DeltaFilterResult::Reject();
        }

        auto slot_result = GetSlotKey( certificate.proposal() );
        auto hash_result = GetSubjectHash( certificate.proposal().subject() );
        if ( slot_result.has_error() || hash_result.has_error() ||
             !IsCanonicalHash( slot_result.value() ) || !IsCanonicalHash( hash_result.value() ) )
        {
            ConsensusManagerLogger()->critical( "{}: certificate pair derivation failed proposal_id={}",
                                                __func__,
                                                certificate.proposal_id() );
            return crdt::DeltaFilterResult::Reject();
        }

        const auto expected_slot_key = std::string( CERTIFICATE_SLOT_BASE_PATH_KEY ) + slot_result.value();
        const auto expected_index_key =
            std::string( CERTIFICATE_TX_INDEX_BASE_PATH_KEY ) + hash_result.value();
        const bool slot_key_matches = slot_element->key() == expected_slot_key ||
                                      slot_element->key() == expected_slot_key.substr( 1 );
        const bool index_key_matches = index_element->key() == expected_index_key ||
                                       index_element->key() == expected_index_key.substr( 1 );
        if ( !slot_key_matches || !index_key_matches || index_element->value() != slot_result.value() )
        {
            ConsensusManagerLogger()->critical(
                "{}: mismatched certificate pair slot={} proposal_id={} winner={} slot_key={} index_key={} "
                "index_value={}",
                __func__,
                slot_result.value(),
                certificate.proposal_id(),
                hash_result.value(),
                slot_element->key(),
                index_element->key(),
                index_element->value() );
            return crdt::DeltaFilterResult::Reject();
        }

        // Full structural, signature, quorum, registry, and deterministic-byte
        // validation must precede occupied-slot conflict classification. Invalid
        // traffic is an ordinary rejection and can never create local safety state.
        auto normalized = NormalizeCertificateStructural( certificate );
        if ( normalized.check == Check::Stalled )
        {
            auto dependency = CID::fromString( certificate.registry_cid() );
            if ( dependency.has_error() ) return crdt::DeltaFilterResult::Reject();
            return crdt::DeltaFilterResult::RetryDependency( certificate.registry_cid() );
        }
        if ( normalized.check != Check::Approve || normalized.deterministic_bytes != slot_element->value() )
        {
            ConsensusManagerLogger()->critical( "{}: noncanonical certificate payload key={}",
                                                __func__, slot_element->key() );
            return crdt::DeltaFilterResult::Reject();
        }
        certificate = normalized.certificate;

        const auto existing_slot_result = ReadCertificatePreflightRecord( { expected_slot_key } );
        const auto existing_index_result = ReadCertificatePreflightRecord( { expected_index_key } );
        if ( existing_slot_result.has_error() || existing_index_result.has_error() )
        {
            ConsensusManagerLogger()->critical(
                "{}: replicated certificate preflight failed closed slot={} winner={} slot_key={} slot_error={} "
                "index_key={} index_error={}",
                __func__,
                slot_result.value(),
                hash_result.value(),
                expected_slot_key,
                existing_slot_result.has_error() ? existing_slot_result.error().message() : "none",
                expected_index_key,
                existing_index_result.has_error() ? existing_index_result.error().message() : "none" );
            return crdt::DeltaFilterResult::Reject();
        }
        const auto &existing_slot  = existing_slot_result.value();
        const auto &existing_index = existing_index_result.value();
        if ( existing_slot )
        {
            if ( std::string( existing_slot->toString() ) != normalized.deterministic_bytes )
            {
                Certificate authoritative;
                if ( !authoritative.ParseFromString( existing_slot->toString() ) )
                    return crdt::DeltaFilterResult::Reject();
                auto authoritative_normalized = NormalizeCertificateStructural( authoritative );
                if ( authoritative_normalized.check != Check::Approve ||
                     authoritative_normalized.deterministic_bytes != existing_slot->toString() )
                    return crdt::DeltaFilterResult::Reject();
                (void) RecordCertificateConflict( authoritative_normalized,
                                                  normalized,
                                                  slot_result.value(),
                                                  DeliverySource::CRDT,
                                                  CurrentTimeMs() );
                ConsensusManagerLogger()->critical(
                    "{}: replicated certificate conflict slot={} incoming_proposal_id={} winner={}",
                    __func__, slot_result.value(), certificate.proposal_id(), hash_result.value() );
                return crdt::DeltaFilterResult::Reject();
            }
            if ( !existing_index || std::string( existing_index->toString() ) != slot_result.value() )
                return crdt::DeltaFilterResult::Reject();
        }
        else if ( existing_index )
            return crdt::DeltaFilterResult::Reject();

        return crdt::DeltaFilterResult::Approve();
    }

    std::optional<std::vector<crdt::pb::Element>> ConsensusManager::FilterCertificate(
        const crdt::pb::Element &element )
    {
        ConsensusManagerLogger()->trace( "{}: entry key={}", __func__, element.key() );
        Certificate certificate;
        if ( !certificate.ParseFromString( element.value() ) )
        {
            ConsensusManagerLogger()->error( "{}: parse failed, rejecting: {}", __func__, element.key() );
            return std::vector<crdt::pb::Element>{};
        }

        auto normalized = NormalizeCertificateStructural( certificate );
        if ( normalized.check == Check::Reject )
        {
            ConsensusManagerLogger()->error( "{}: validation failed, rejecting: {}", __func__, element.key() );
            return std::vector<crdt::pb::Element>{};
        }
        if ( normalized.check == Check::Stalled )
        {
            return std::nullopt;
        }
        if ( normalized.deterministic_bytes != element.value() )
        {
            ConsensusManagerLogger()->error( "{}: noncanonical bytes, rejecting: {}", __func__, element.key() );
            return std::vector<crdt::pb::Element>{};
        }
        certificate = std::move( normalized.certificate );

        auto slot_result = GetSlotKey( certificate.proposal() );
        if ( slot_result.has_error() ||
             element.key() != std::string( CERTIFICATE_SLOT_BASE_PATH_KEY ) + slot_result.value() )
        {
            ConsensusManagerLogger()->critical( "{}: slot key mismatch, rejecting key={} proposal_id={}",
                                                __func__,
                                                element.key(),
                                                certificate.proposal_id() );
            return std::vector<crdt::pb::Element>{};
        }

        ConsensusManagerLogger()->debug( "{}: certificate accepted key={}", __func__, element.key() );
        return std::nullopt;
    }

    void ConsensusManager::CertificateReceived( const crdt::CRDTCallbackManager::NewDataPair &new_data,
                                                const std::string & )
    {
        const auto &[key, value] = new_data;
        Certificate certificate;
        if ( !certificate.ParseFromArray( value.data(), value.size() ) )
        {
            ConsensusManagerLogger()->error( "{}: invalid certificate payload key={}", __func__, key );
            return;
        }

        auto slot_result = GetSlotKey( certificate.proposal() );
        if ( slot_result.has_error() || key != std::string( CERTIFICATE_SLOT_BASE_PATH_KEY ) + slot_result.value() )
            return;

        // CRDT invokes new-element callbacks synchronously while the incoming
        // delta is still being merged.  FinalizeSlot may publish the canonical
        // slot/index pair when its preflight cannot yet observe that in-flight
        // merge.  Calling it here would therefore enqueue a nested DAG job and
        // wait for that job from the worker which must finish the outer job: a
        // GraphSync teardown/deadline deadlock.  Leave the exact callback key in
        // the durable journal and let the owned recovery turn run only after the
        // merge has committed and the authoritative pair is readable.
        certificate_work_journal_->MarkSeen( key );
        certificate_work_journal_->MarkStalled( key, std::chrono::milliseconds( 0 ) );
    }

    ConsensusManager::CertificateNormalization ConsensusManager::NormalizeCertificateStructural(
        const Certificate &certificate ) const
    {
        CertificateNormalization result;
        if ( HasUnknownFieldsRecursively( certificate ) )
        {
            ConsensusManagerLogger()->error( "{}: rejected: certificate contains protobuf unknown fields",
                                             __func__ );
            return result;
        }
        if ( certificate.proposal_id().empty() || !certificate.has_proposal() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: certificate proposal is incomplete", __func__ );
            return result;
        }

        const auto &proposal = certificate.proposal();
        if ( proposal.proposal_id() != certificate.proposal_id() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: proposal_id mismatch cert={} proposal={}",
                                             __func__,
                                             certificate.proposal_id(),
                                             proposal.proposal_id() );
            return result;
        }
        if ( proposal.registry_cid() != certificate.registry_cid() ||
             proposal.registry_epoch() != certificate.registry_epoch() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: registry mismatch proposal_id={}",
                                             __func__,
                                             certificate.proposal_id() );
            return result;
        }

        auto registry_ret = registry_->LoadRegistryByCid( certificate.registry_cid() );
        if ( registry_ret.has_error() )
        {
            if ( registry_ret.error() == std::errc::no_such_file_or_directory )
            {
                ConsensusManagerLogger()->error(
                    "{}: stalled: registry missing for registry cid {} proposal_id={}",
                    __func__,
                    certificate.registry_cid(),
                    certificate.proposal_id() );
                result.check = Check::Stalled;
            }
            else
            {
                ConsensusManagerLogger()->error(
                    "{}: rejected: registry load error={} for registry cid {} proposal_id={}",
                    __func__,
                    registry_ret.error().message(),
                    certificate.registry_cid(),
                    certificate.proposal_id() );
            }
            return result;
        }
        auto &registry = registry_ret.value();
        if ( registry.epoch() != certificate.registry_epoch() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: registry epoch mismatch proposal_id={}",
                                             __func__,
                                             certificate.proposal_id() );
            return result;
        }
        if ( !ValidateSubject( proposal.subject() ) || !CheckProposal( proposal ) )
        {
            ConsensusManagerLogger()->error( "{}: rejected: invalid signed proposal proposal_id={}",
                                             __func__,
                                             proposal.proposal_id() );
            return result;
        }

        const auto computed_id = CreateProposalId( proposal );
        if ( computed_id.empty() || computed_id != certificate.proposal_id() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: computed proposal id mismatch cert={} computed={}",
                                             __func__,
                                             certificate.proposal_id(),
                                             computed_id );
            return result;
        }

        std::vector<Vote> votes;
        votes.reserve( static_cast<size_t>( certificate.votes_size() ) );
        for ( const auto &vote : certificate.votes() )
        {
            votes.push_back( vote );
        }
        if ( votes.empty() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: certificate contains no votes proposal_id={}",
                                             __func__,
                                             certificate.proposal_id() );
            return result;
        }

        auto tally = TallyVotes( proposal, votes, registry, certificate.registry_cid() );
        if ( tally.has_error() || !tally.value().has_quorum )
        {
            return result;
        }

        std::sort( votes.begin(), votes.end(), VoterIdBytewiseLess );
        uint64_t max_vote_timestamp = 0;
        for ( const auto &vote : votes )
        {
            max_vote_timestamp = std::max( max_vote_timestamp, vote.timestamp() );
        }
        if ( max_vote_timestamp == 0 )
        {
            ConsensusManagerLogger()->error( "{}: rejected: certificate votes have no timestamp proposal_id={}",
                                             __func__,
                                             certificate.proposal_id() );
            return result;
        }

        result.certificate = certificate;
        result.certificate.clear_votes();
        for ( const auto &vote : votes )
        {
            *result.certificate.add_votes() = vote;
        }
        result.certificate.set_total_weight( tally.value().total_weight );
        result.certificate.set_approved_weight( tally.value().approved_weight );
        result.certificate.set_timestamp( max_vote_timestamp );

        auto bytes = SerializeCertificateDeterministically( result.certificate );
        if ( bytes.has_error() )
        {
            return result;
        }
        result.deterministic_bytes = std::move( bytes.value() );
        result.check               = Check::Approve;
        return result;
    }

    ConsensusManager::Check ConsensusManager::ValidateCertificate( const Certificate &certificate ) const
    {
        return NormalizeCertificateStructural( certificate ).check;
    }

    ConsensusManager::Check ConsensusManager::ValidateCertificateForFirstObservation(
        const CertificateNormalization &normalized, uint64_t system_now_ms ) const
    {
        if ( normalized.check != Check::Approve ) return normalized.check;
        const auto window = static_cast<uint64_t>( timestamp_window_.count() );
        const auto accepted = [system_now_ms, window]( uint64_t timestamp )
        {
            if ( timestamp == 0 ) return false;
            const auto lower = system_now_ms > window ? system_now_ms - window : 0;
            const auto upper = std::numeric_limits<uint64_t>::max() - system_now_ms < window
                                 ? std::numeric_limits<uint64_t>::max()
                                 : system_now_ms + window;
            return timestamp >= lower && timestamp <= upper;
        };
        if ( !accepted( normalized.certificate.proposal().timestamp() ) ) return Check::Reject;
        for ( const auto &vote : normalized.certificate.votes() )
            if ( !accepted( vote.timestamp() ) ) return Check::Reject;
        return Check::Approve;
    }

    void ConsensusManager::HandleVote( const Vote &vote )
    {
        const auto &proposal_id = vote.proposal_id();
        const auto &voter_id    = vote.voter_id();
        if ( !CheckVote( vote ) )
        {
            return;
        }
        if ( !vote.approve() )
        {
            ConsensusManagerLogger()->debug( "{}: ignored: vote not approved voter_id={}",
                                             __func__,
                                             vote.voter_id().substr( 0, 8 ) );
            return;
        }
        {
            std::lock_guard lock( proposals_mutex_ );
            if ( restored_safety_proposal_ids_.count( vote.proposal_id() ) != 0 ) return;
        }

        auto signing_bytes = sgns::VoteSigningBytes( vote );
        if ( signing_bytes.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: signing bytes error={}",
                                             __func__,
                                             signing_bytes.error().message() );
            return;
        }
        if ( !GeniusAccount::VerifySignature( voter_id, vote.signature(), signing_bytes.value() ) )
        {
            ConsensusManagerLogger()->error( "{}: rejected: signature verification failed voter_id={}",
                                             __func__,
                                             voter_id.substr( 0, 8 ) );
            return;
        }

        bool reached_quorum = false;
        {
            std::lock_guard lock( proposals_mutex_ );
            auto            it = proposals_.find( proposal_id );
            if ( it == proposals_.end() )
            {
                pending_votes_[proposal_id].push_back( vote );
                ConsensusManagerLogger()->debug( "{}: queued pending vote proposal_id={}",
                                                 __func__,
                                                 proposal_id.substr( 0, 8 ) );
                return;
            }

            auto &state = it->second;
            if ( restored_safety_slots_.count( state.slot_key ) != 0 )
            {
                ConsensusManagerLogger()->critical(
                    "{}: vote aggregation blocked by slot safety violation slot={} proposal_id={}",
                    __func__, state.slot_key, proposal_id );
                return;
            }

            const auto &proposal     = state.proposal;
            auto        subject_hash = GetSubjectHash( proposal.subject() );
            if ( subject_hash.has_value() && CheckCertificateForSubject( subject_hash.value() ) )
            {
                pending_votes_.erase( proposal_id );
                return;
            }

            auto slot_it = slot_states_.find( state.slot_key );
            if ( slot_it != slot_states_.end() && slot_it->second.best_proposal_id != proposal_id )
            {
                return;
            }
            if ( state.seen_voters.find( voter_id ) != state.seen_voters.end() )
            {
                return;
            }

            auto proposal_registry_result = registry_->LoadRegistryByCid( proposal.registry_cid() );
            if ( proposal_registry_result.has_error() )
            {
                ConsensusManagerLogger()->warn( "{}: deferred vote: registry load error={} proposal_id={}",
                                                __func__,
                                                proposal_registry_result.error().message(),
                                                proposal_id.substr( 0, 8 ) );
                pending_votes_[proposal_id].push_back( vote );
                return;
            }
            const auto &proposal_registry = proposal_registry_result.value();
            if ( proposal.registry_epoch() != proposal_registry.epoch() )
            {
                ConsensusManagerLogger()->error( "{}: rejected: registry mismatch proposal_id={}",
                                                 __func__,
                                                 proposal_id.substr( 0, 8 ) );
                return;
            }

            state.votes.push_back( vote );
            state.seen_voters.insert( voter_id );
            if ( state.quorum_reached )
            {
                return;
            }

            // ponytail: recomputing the tally makes admission O(votes^2); restore an incremental tally if validator
            // sets grow enough for this to matter.
            auto tally = EvaluateQuorum( proposal, state.votes, proposal_registry );
            if ( tally.has_error() )
            {
                ConsensusManagerLogger()->error( "{}: quorum evaluation failed proposal_id={} error={}",
                                                 __func__,
                                                 proposal_id.substr( 0, 8 ),
                                                 tally.error().message() );
                return;
            }
            if ( !tally.value().has_quorum )
            {
                return;
            }

            state.quorum_reached       = true;
            state.quorum_reached_ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::system_clock::now().time_since_epoch() )
                                             .count();
            reached_quorum             = true;
        }

        if ( reached_quorum )
        {
            certificates_pending_.store( true );
            timer_cv_.notify_all();
        }
    }

    void ConsensusManager::HandleCertificate( const Certificate &certificate )
    {
        (void) FinalizeSlot( certificate, DeliverySource::PubSub );
    }

    outcome::result<ConsensusManager::ProposalState> ConsensusManager::FetchProposalState(
        const Certificate &certificate )
    {
        std::lock_guard lock( proposals_mutex_ );
        auto            it = proposals_.find( certificate.proposal_id() );
        if ( it == proposals_.end() )
        {
            return outcome::failure( std::errc::no_such_device );
        }
        return it->second;
    }

    ConsensusManager::ProposalState ConsensusManager::CreateProposalState( const Certificate   &certificate,
                                                                           const std::string   &slot_key )
    {
        ProposalState new_state;
        new_state.proposal = certificate.proposal();
        new_state.slot_key = slot_key;
        proposals_.emplace( new_state.proposal.proposal_id(), new_state );

        auto &slot_state = slot_states_[new_state.slot_key];
        if ( slot_state.best_proposal_id.empty() )
        {
            slot_state.best_proposal_id = new_state.proposal.proposal_id();
        }

        return new_state;
    }

    std::vector<ConsensusManager::Vote> ConsensusManager::CollectCertificateVotes(
        const Certificate &certificate ) const
    {
        std::vector<Vote> votes;
        votes.reserve( static_cast<size_t>( certificate.votes_size() ) );
        for ( const auto &vote : certificate.votes() )
        {
            ConsensusManagerLogger()->trace( "{}: processing vote voter_id={}", __func__, vote.voter_id() );
            votes.push_back( vote );
        }
        return votes;
    }

    void ConsensusManager::ClearProposalSlot( const Proposal &proposal )
    {
        std::string slot_key;
        std::vector<Proposal> cleaned_proposals;
        {
            std::lock_guard lock( proposals_mutex_ );
            auto it = proposals_.find( proposal.proposal_id() );
            if ( it != proposals_.end() )
                slot_key = it->second.slot_key;
            else
            {
                auto slot_result = GetSlotKey( proposal );
                if ( slot_result.has_error() ) return;
                slot_key = slot_result.value();
            }

            std::unordered_set<std::string> ids_to_remove{ proposal.proposal_id() };
            for ( const auto &kv : proposals_ )
            {
                if ( kv.second.slot_key == slot_key ) ids_to_remove.insert( kv.first );
            }

            for ( const auto &proposal_id : ids_to_remove )
            {
                auto proposal_it = proposals_.find( proposal_id );
                if ( proposal_it != proposals_.end() ) cleaned_proposals.push_back( proposal_it->second.proposal );
                RemovePendingProposalLocked( proposal_id, "slot-cleanup" );
                pending_votes_.erase( proposal_id );
                proposals_.erase( proposal_id );
            }

            auto &slot_state = slot_states_[slot_key];
            if ( slot_state.lifecycle != SlotState::Lifecycle::Applied &&
                 slot_state.lifecycle != SlotState::Lifecycle::SafetyViolation )
                slot_state.lifecycle = SlotState::Lifecycle::FinalizedPendingApplication;
            slot_state.reserved_finalization_proposal_id = proposal.proposal_id();
            restored_final_slots_.insert( slot_key );

            bool has_pending = false;
            for ( const auto &kv : proposals_ )
            {
                if ( kv.second.quorum_reached )
                {
                    has_pending = true;
                    break;
                }
            }
            certificates_pending_.store( has_pending );
            if ( !has_pending ) timer_cv_.notify_all();
        }
        if ( cleaned_proposals.empty() ) cleaned_proposals.push_back( proposal );
        for ( const auto &cleaned : cleaned_proposals ) FireProposalCleanupCallbacks( cleaned );
    }

    outcome::result<std::string> ConsensusManager::GetSlotKey( const Proposal &proposal )
    {
        ConsensusManagerLogger()->trace( "{}: called proposal_id={}", __func__, proposal.proposal_id() );
        return GetSlotKey( proposal.subject() );
    }

    outcome::result<std::string> ConsensusManager::GetSlotKey( const Subject &subject )
    {
        auto type_hash = ParseSubjectTypeHash( subject );
        if ( !type_hash )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        SlotKeyHandler handler;
        {
            std::shared_lock lock( slot_key_handlers_mutex_ );
            auto             it = slot_key_handlers_.find( type_hash.value() );
            if ( it != slot_key_handlers_.end() )
            {
                handler = it->second;
            }
        }

        auto slot_result = handler ? handler( subject ) : ComputeSubjectId( subject );

        if ( slot_result.has_error() )
        {
            return outcome::failure( slot_result.error() );
        }

        const auto &slot = slot_result.value();
        const bool  canonical_slot =
            slot.size() == 64 &&
            std::all_of(
                slot.begin(),
                slot.end(),
                []( unsigned char c )
                {
                    return std::isdigit( c ) != 0 || ( c >= 'a' && c <= 'f' );
                } );
        if ( !canonical_slot )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        return slot;
    }

    bool ConsensusManager::IsBetterProposal( const Proposal &candidate, const Proposal &current ) const
    {
        ConsensusManagerLogger()->trace( "{}: called candidate={} current={}",
                                         __func__,
                                         candidate.proposal_id(),
                                         current.proposal_id() );
        auto candidate_nonce = DecodeNonceSubject( candidate.subject() );
        auto current_nonce   = DecodeNonceSubject( current.subject() );
        if ( candidate_nonce.has_value() && current_nonce.has_value() )
        {
            const auto &cand_hash = candidate_nonce.value().tx_hash();
            const auto &curr_hash = current_nonce.value().tx_hash();
            if ( cand_hash == curr_hash )
            {
                return candidate.proposal_id() < current.proposal_id();
            }
            return BestHash( curr_hash, cand_hash ) == cand_hash;
        }

        return candidate.proposal_id() < current.proposal_id();
    }

    const std::string &ConsensusManager::BestHash( const std::string &a, const std::string &b )
    {
        return ( a <= b ) ? a : b;
    }

    outcome::result<std::string> ConsensusManager::ComputeSubjectId( const Subject &subject )
    {
        ConsensusManagerLogger()->trace( "{}: called", __func__ );
        std::string serialized;
        if ( !subject.SerializeToString( &serialized ) )
        {
            ConsensusManagerLogger()->error( "{}: failed: serialization error", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }

        auto hash = sgns::crypto::sha2_256( serialized.data(), serialized.size() );
        ConsensusManagerLogger()->debug( "{}: success", __func__ );
        return base::hex_lower( gsl::span<const uint8_t>( hash.data(), hash.size() ) );
    }

    namespace
    {
        constexpr size_t kSubjectTypeHashSize = base::Hash256::size();

        base::Hash256 ComputePayloadHash( std::string_view payload )
        {
            return sgns::crypto::sha2_256( payload.data(), payload.size() );
        }

        outcome::result<void> SetSubjectPayload( ConsensusSubject                    &subject,
                                                 const base::Hash256                 &subject_type_hash,
                                                 const google::protobuf::MessageLite &payload )
        {
            std::string serialized;
            if ( !payload.SerializeToString( &serialized ) )
            {
                return outcome::failure( std::errc::invalid_argument );
            }
            std::string canonical_payload = subject_type_hash.toString() + serialized;
            const auto  payload_hash      = ComputePayloadHash( canonical_payload );
            subject.mutable_subject_type_hash()->set_hash( subject_type_hash.data(), subject_type_hash.size() );
            subject.set_payload( canonical_payload.data(), canonical_payload.size() );
            subject.set_payload_hash( payload_hash.data(), payload_hash.size() );
            return outcome::success();
        }

        outcome::result<std::string_view> ExtractBuiltinPayload( const ConsensusSubject &subject,
                                                                 std::string_view        subject_type )
        {
            BOOST_OUTCOME_TRY( auto expected, ConsensusManager::ComputeSubjectTypeHash( subject_type ) );
            const auto expected_bytes = expected.toString();
            if ( !subject.has_subject_type_hash() || subject.subject_type_hash().hash() != expected_bytes ||
                 subject.payload().size() <= kSubjectTypeHashSize ||
                 subject.payload().compare( 0, kSubjectTypeHashSize, expected_bytes ) != 0 )
            {
                return outcome::failure( std::errc::invalid_argument );
            }
            return std::string_view( subject.payload().data() + kSubjectTypeHashSize,
                                     subject.payload().size() - kSubjectTypeHashSize );
        }

        template <typename Payload>
        outcome::result<Payload> DecodeBuiltinSubject( const ConsensusSubject &subject, std::string_view subject_type )
        {
            BOOST_OUTCOME_TRY( auto raw_payload, ExtractBuiltinPayload( subject, subject_type ) );
            Payload payload;
            if ( raw_payload.size() > std::numeric_limits<int>::max() ||
                 !payload.ParseFromArray( raw_payload.data(), static_cast<int>( raw_payload.size() ) ) )
            {
                return outcome::failure( std::errc::invalid_argument );
            }
            return payload;
        }
    }

    outcome::result<base::Hash256> ConsensusManager::ComputeSubjectTypeHash( std::string_view subject_type )
    {
        if ( subject_type.empty() )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        return sgns::crypto::sha2_256( subject_type.data(), subject_type.size() );
    }

    outcome::result<NonceSubject> ConsensusManager::DecodeNonceSubject( const Subject &subject )
    {
        return DecodeBuiltinSubject<NonceSubject>( subject, NONCE_SUBJECT_TYPE );
    }

    outcome::result<TaskResultSubject> ConsensusManager::DecodeTaskResultSubject( const Subject &subject )
    {
        return DecodeBuiltinSubject<TaskResultSubject>( subject, TASK_RESULT_SUBJECT_TYPE );
    }

    outcome::result<RegistryBatchSubject> ConsensusManager::DecodeRegistryBatchSubject( const Subject &subject )
    {
        return DecodeBuiltinSubject<RegistryBatchSubject>( subject, REGISTRY_BATCH_SUBJECT_TYPE );
    }

    outcome::result<ConsensusManager::Subject> ConsensusManager::CreateNonceSubject(
        const std::string                             &account_id,
        uint64_t                                       nonce,
        const std::string                             &tx_hash,
        const EmbeddedTransaction                     &transaction,
        const std::optional<UTXOTransitionCommitment> &utxo_commitment,
        const std::optional<UTXOWitness>              &utxo_witness )
    {
        ConsensusManagerLogger()->trace( "{}: called account_id={} nonce={}", __func__, account_id, nonce );
        Subject subject;
        subject.set_account_id( account_id );
        NonceSubject payload;
        payload.set_nonce( nonce );
        payload.set_tx_hash( tx_hash.data(), tx_hash.size() );
        *payload.mutable_transaction() = transaction;
        if ( utxo_commitment.has_value() )
        {
            *payload.mutable_utxo_commitment() = utxo_commitment.value();
        }
        if ( utxo_witness.has_value() )
        {
            *payload.mutable_utxo_witness() = utxo_witness.value();
        }
        BOOST_OUTCOME_TRY( auto type_hash, ComputeSubjectTypeHash( NONCE_SUBJECT_TYPE ) );
        BOOST_OUTCOME_TRY( SetSubjectPayload( subject, type_hash, payload ) );

        ConsensusManagerLogger()->debug( "{}: success", __func__ );
        return subject;
    }

    outcome::result<ConsensusManager::Subject> ConsensusManager::CreateTaskResultSubject(
        const std::string &account_id,
        const std::string &escrow_path,
        const std::string &task_result_hash,
        uint64_t           result_epoch )
    {
        ConsensusManagerLogger()->trace( "{}: called account_id={} result_epoch={}",
                                         __func__,
                                         account_id,
                                         result_epoch );
        Subject subject;
        subject.set_account_id( account_id );
        TaskResultSubject payload;
        payload.set_escrow_path( escrow_path );
        payload.set_task_result_hash( task_result_hash.data(), task_result_hash.size() );
        payload.set_result_epoch( result_epoch );
        BOOST_OUTCOME_TRY( auto type_hash, ComputeSubjectTypeHash( TASK_RESULT_SUBJECT_TYPE ) );
        BOOST_OUTCOME_TRY( SetSubjectPayload( subject, type_hash, payload ) );

        ConsensusManagerLogger()->debug( "{}: success", __func__ );
        return subject;
    }

    outcome::result<ConsensusManager::Subject> ConsensusManager::CreateRegistryBatchSubject(
        const std::string &account_id,
        const std::string &base_registry_cid,
        uint64_t           base_registry_epoch,
        uint64_t           target_registry_epoch,
        uint32_t           certificate_count,
        const std::string &batch_root )
    {
        ConsensusManagerLogger()->trace( "{}: called account_id={} base_epoch={} target_epoch={} certificates={}",
                                         __func__,
                                         account_id.substr( 0, 8 ),
                                         base_registry_epoch,
                                         target_registry_epoch,
                                         certificate_count );
        Subject subject;
        subject.set_account_id( account_id );
        RegistryBatchSubject payload;
        payload.set_base_registry_cid( base_registry_cid );
        payload.set_base_registry_epoch( base_registry_epoch );
        payload.set_target_registry_epoch( target_registry_epoch );
        payload.set_certificate_count( certificate_count );
        payload.set_batch_root( batch_root.data(), batch_root.size() );
        BOOST_OUTCOME_TRY( auto type_hash, ComputeSubjectTypeHash( REGISTRY_BATCH_SUBJECT_TYPE ) );
        BOOST_OUTCOME_TRY( SetSubjectPayload( subject, type_hash, payload ) );

        ConsensusManagerLogger()->debug( "{}: success", __func__ );
        return subject;
    }

    outcome::result<ConsensusManager::Subject> ConsensusManager::CreateGenericSubject(
        const std::string          &account_id,
        std::string_view            subject_type,
        const std::vector<uint8_t> &payload )
    {
        ConsensusManagerLogger()->trace( "{}: called account_id={} subject_type={}",
                                         __func__,
                                         account_id.substr( 0, 8 ),
                                         subject_type );
        if ( account_id.empty() || subject_type.empty() || payload.empty() )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        Subject subject;
        subject.set_account_id( account_id );
        subject.set_payload( payload.data(), payload.size() );
        auto type_hash = ComputeSubjectTypeHash( subject_type );
        if ( type_hash.has_error() )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        const auto payload_hash = ComputePayloadHash( subject.payload() );
        subject.set_payload_hash( payload_hash.data(), payload_hash.size() );
        subject.mutable_subject_type_hash()->set_hash( type_hash.value().data(), type_hash.value().size() );
        ConsensusManagerLogger()->debug( "{}: success", __func__ );
        return subject;
    }

    std::string ConsensusManager::CreateProposalId( const Proposal &proposal )
    {
        ConsensusManagerLogger()->trace( "{}: Creating proposal ID", __func__ );
        // Proposal ID must be derived from the proposal contents excluding the proposal_id itself.
        Proposal copy = proposal;
        copy.clear_proposal_id();
        auto signing_bytes = sgns::ProposalSigningBytes( copy );
        if ( signing_bytes.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed, no proposal ID created: signing bytes error={}",
                                             __func__,
                                             signing_bytes.error().message() );
            return {};
        }

        auto hash        = sgns::crypto::sha2_256( signing_bytes.value().data(), signing_bytes.value().size() );
        auto proposal_id = base::hex_lower( gsl::span<const uint8_t>( hash.data(), hash.size() ) );
        ConsensusManagerLogger()->debug( "{}: Proposal ID {} created", __func__, proposal_id.substr( 0, 8 ) );
        return proposal_id;
    }

    bool ConsensusManager::ValidateSubject( const Subject &subject )
    {
        ConsensusManagerLogger()->trace( "{}: called", __func__ );
        if ( subject.account_id().empty() )
        {
            return false;
        }
        if ( !ParseSubjectTypeHash( subject ) )
        {
            return false;
        }
        if ( subject.payload().empty() || subject.payload_hash().empty() )
        {
            return false;
        }
        if ( ComputePayloadHash( subject.payload() ).toString() != subject.payload_hash() )
        {
            return false;
        }

        switch ( GetBuiltinSubjectKind( subject ) )
        {
            case BuiltinSubjectKind::Nonce:
            {
                auto payload = DecodeNonceSubject( subject );
                return payload.has_value() && !payload.value().tx_hash().empty() &&
                       ( !payload.value().has_utxo_witness() || payload.value().has_utxo_commitment() );
            }
            case BuiltinSubjectKind::TaskResult:
            {
                auto payload = DecodeTaskResultSubject( subject );
                return payload.has_value() && !payload.value().task_result_hash().empty();
            }
            case BuiltinSubjectKind::RegistryBatch:
            {
                auto payload = DecodeRegistryBatchSubject( subject );
                return payload.has_value() && !payload.value().base_registry_cid().empty() &&
                       payload.value().target_registry_epoch() == payload.value().base_registry_epoch() + 1 &&
                       payload.value().certificate_count() > 0 && !payload.value().batch_root().empty();
            }
            case BuiltinSubjectKind::Other:
                return true;
        }
        return false;
    }

    void ConsensusManager::OnConsensusMessage( boost::optional<const ipfs_pubsub::GossipPubSub::Message &> message )
    {
        ConsensusManagerLogger()->trace( "{}: called", __func__ );
        if ( !message )
        {
            ConsensusManagerLogger()->error( "{}: ignored: message is empty", __func__ );
            return;
        }

        ConsensusMessage decoded;
        if ( !decoded.ParseFromArray( message->data.data(), static_cast<int>( message->data.size() ) ) )
        {
            ConsensusManagerLogger()->error( "{}: Failed to decode consensus message", __func__ );
            return;
        }
        if ( decoded.has_proposal() )
        {
            ConsensusManagerLogger()->debug( "{}: decoded proposal", __func__ );
            HandleProposal( decoded.proposal() );
            return;
        }
        if ( decoded.has_vote() )
        {
            ConsensusManagerLogger()->debug( "{}: decoded vote", __func__ );
            HandleVote( decoded.vote() );
            return;
        }
        if ( decoded.has_certificate() )
        {
            ConsensusManagerLogger()->debug( "{}: decoded certificate", __func__ );
            HandleCertificate( decoded.certificate() );
        }
    }

    bool ConsensusManager::CheckSubject( const Subject &subject )
    {
        ConsensusManagerLogger()->trace( "{}: called", __func__ );

        if ( subject.account_id().empty() )
        {
            ConsensusManagerLogger()->error( "{}: subject account_id is empty", __func__ );
            return false;
        }
        if ( !ParseSubjectTypeHash( subject ) )
        {
            ConsensusManagerLogger()->error( "{}: subject subject_type_hash is invalid", __func__ );
            return false;
        }
        if ( subject.payload().empty() )
        {
            ConsensusManagerLogger()->error( "{}: subject payload is empty", __func__ );
            return false;
        }
        if ( subject.payload_hash().empty() )
        {
            ConsensusManagerLogger()->error( "{}: subject payload_hash is empty", __func__ );
            return false;
        }
        if ( ComputePayloadHash( subject.payload() ).toString() != subject.payload_hash() )
        {
            ConsensusManagerLogger()->error( "{}: subject payload_hash mismatch", __func__ );
            return false;
        }

        switch ( GetBuiltinSubjectKind( subject ) )
        {
            case BuiltinSubjectKind::Nonce:
            {
                auto payload = DecodeNonceSubject( subject );
                if ( payload.has_error() || payload.value().tx_hash().empty() )
                {
                    ConsensusManagerLogger()->error( "{}: subject nonce tx_hash is empty", __func__ );
                    return false;
                }
                return true;
            }
            case BuiltinSubjectKind::TaskResult:
            {
                auto payload = DecodeTaskResultSubject( subject );
                if ( payload.has_error() || payload.value().escrow_path().empty() )
                {
                    ConsensusManagerLogger()->error( "{}: subject task_result escrow_path is empty", __func__ );
                    return false;
                }
                if ( payload.value().task_result_hash().empty() )
                {
                    ConsensusManagerLogger()->error( "{}: subject task_result task_result_hash is empty", __func__ );
                    return false;
                }
                return true;
            }
            case BuiltinSubjectKind::RegistryBatch:
            {
                auto payload = DecodeRegistryBatchSubject( subject );
                if ( payload.has_error() )
                {
                    return false;
                }
                if ( payload.value().base_registry_cid().empty() )
                {
                    ConsensusManagerLogger()->error( "{}: subject registry_batch base_registry_cid is empty",
                                                     __func__ );
                    return false;
                }
                if ( payload.value().target_registry_epoch() != payload.value().base_registry_epoch() + 1 )
                {
                    ConsensusManagerLogger()->error( "{}: subject registry_batch target epoch mismatch", __func__ );
                    return false;
                }
                if ( payload.value().certificate_count() == 0 )
                {
                    ConsensusManagerLogger()->error( "{}: subject registry_batch certificate_count is zero", __func__ );
                    return false;
                }
                if ( payload.value().batch_root().empty() )
                {
                    ConsensusManagerLogger()->error( "{}: subject registry_batch batch_root is empty", __func__ );
                    return false;
                }
                return true;
            }
            case BuiltinSubjectKind::Other:
                return true;
        }
        return false;
    }

    bool ConsensusManager::CheckProposal( const Proposal &proposal )
    {
        if ( proposal.proposal_id().empty() )
        {
            ConsensusManagerLogger()->error( "{}: Proposal ID missing ", __func__ );
            return false;
        }
        if ( proposal.proposer_id().empty() )
        {
            ConsensusManagerLogger()->error( "{}: Proposer ID missing ", __func__ );
            return false;
        }
        if ( proposal.registry_cid().empty() )
        {
            ConsensusManagerLogger()->error( "{}: Registry CID missing ", __func__ );
            return false;
        }
        if ( !proposal.has_subject() )
        {
            ConsensusManagerLogger()->error( "{}: Proposal without subject ", __func__ );
            return false;
        }
        auto signing_bytes = sgns::ProposalSigningBytes( proposal );
        if ( signing_bytes.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: rejected: signing bytes error={}",
                                             __func__,
                                             signing_bytes.error().message() );
            return false;
        }
        if ( !GeniusAccount::VerifySignature( proposal.proposer_id(), proposal.signature(), signing_bytes.value() ) )
        {
            ConsensusManagerLogger()->error( "{}: rejected: signature verification failed proposer_id={}",
                                             __func__,
                                             proposal.proposer_id() );
            return false;
        }
        return true;
    }

    bool ConsensusManager::CheckVote( const Vote &vote )
    {
        if ( vote.proposal_id().empty() )
        {
            ConsensusManagerLogger()->error( "{}: Vote proposal ID missing ", __func__ );
            return false;
        }
        if ( vote.voter_id().empty() )
        {
            ConsensusManagerLogger()->error( "{}: Vote voter ID missing ", __func__ );
            return false;
        }
        return true;
    }

    void ConsensusManager::RecoverPendingCertificateWork()
    {
        auto recovered = certificate_work_journal_->RecoverStaleProcessing( CERT_SLOT_KEY_PATTERN,
                                                                            std::chrono::seconds( 15 ) );
        if ( recovered > 0 )
        {
            ConsensusManagerLogger()->info( "{}: recovered {} stale certificate work items", __func__, recovered );
        }

        auto       unfinished = certificate_work_journal_->ListUnfinished( CERT_SLOT_KEY_PATTERN );
        const auto now_ms     = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );

        for ( const auto &entry : unfinished )
        {
            if ( entry.key.empty() )
            {
                continue;
            }
            if ( entry.state != crdt::CRDTWorkJournal::State::Stalled )
            {
                continue;
            }
            if ( entry.lease_until_ms != 0 && entry.lease_until_ms > now_ms )
            {
                continue;
            }
            auto value = db_->Get( { entry.key } );
            if ( value.has_error() )
            {
                continue;
            }
            Certificate certificate;
            if ( certificate.ParseFromArray( value.value().data(), value.value().size() ) )
                (void) FinalizeSlot( certificate, DeliverySource::Recovery );
        }
    }

    void ConsensusManager::RecoverRestoredCertificateWork()
    {
        std::vector<std::string> pending_slots;
        {
            std::lock_guard lock( restored_state_mutex_ );
            for ( const auto &[slot, process] : restored_processes_ )
            {
                // A safety stop blocks new participation, but FinalizeSlot
                // still admits the exact authoritative winner so its pending
                // application can recover without reopening consensus.
                if ( process.state() == ConsensusStateStore::ProcessRecord::PENDING )
                    pending_slots.push_back( slot );
            }
        }
        for ( const auto &slot : pending_slots )
        {
            const auto key = std::string( CERTIFICATE_SLOT_BASE_PATH_KEY ) + slot;
            auto value = db_->Get( { key } );
            if ( value )
            {
                Certificate certificate;
                if ( certificate.ParseFromArray( value.value().data(), value.value().size() ) )
                    (void) FinalizeSlot( certificate, DeliverySource::Recovery );
            }
        }
    }

    void ConsensusManager::ReplayRestoredVotes()
    {
        const auto now = CurrentTimeMs();
        for ( const auto &record : restored_votes_ )
        {
            if ( record.state() != ConsensusStateStore::VoteRecord::ACTIVE ||
                 record.acceptance_horizon_ms() < now || restored_final_slots_.count( record.slot_id() ) != 0 ||
                 restored_safety_slots_.count( record.slot_id() ) != 0 )
                continue;
            (void) ReplayDurableVote( record.slot_id(), record.generation() );
        }
    }

    bool ConsensusManager::HasCompatibleCertificateState() const
    {
        static const std::regex slot_regex{ std::string( CERT_SLOT_KEY_PATTERN ) };
        static const std::regex index_regex{ std::string( CERT_TX_INDEX_KEY_PATTERN ) };

        auto certificate_entries = db_->QueryKeyValues( CERTIFICATE_BASE_PATH_KEY );
        if ( certificate_entries.has_error() )
        {
            ConsensusManagerLogger()->critical(
                "{}: unable to inspect certificate state for protocol v2.0 compatibility error={}",
                __func__,
                certificate_entries.error().message() );
            return false;
        }

        for ( const auto &[stored_key, unused_value] : certificate_entries.value() )
        {
            (void) unused_value;
            auto key_result = db_->KeyToString( stored_key );
            if ( key_result.has_error() )
            {
                ConsensusManagerLogger()->critical(
                    "{}: unable to decode certificate-state key for protocol v2.0 compatibility error={}",
                    __func__,
                    key_result.error().message() );
                return false;
            }

            const auto &key = key_result.value();
            if ( !std::regex_match( key, slot_regex ) && !std::regex_match( key, index_regex ) )
            {
                ConsensusManagerLogger()->critical(
                    "{}: incompatible legacy or malformed certificate state for protocol v2.0 key={}; "
                    "migration and dual-read are not supported",
                    __func__,
                    key );
                return false;
            }
        }
        return true;
    }

    outcome::result<ConsensusManager::Certificate> ConsensusManager::GetCertificateBySlotId(
        const std::string &slot_id ) const
    {
        if ( !IsCanonicalHash( slot_id ) )
        {
            return outcome::failure( CertificateStoreError::InvalidInput );
        }

        const auto slot_key = std::string{ CERTIFICATE_SLOT_BASE_PATH_KEY } + slot_id;
        auto       certificate_data_result = certificate_record_reader_( { slot_key } );
        if ( certificate_data_result.has_error() )
        {
            const auto mapped = MapCertificateReadError( certificate_data_result.error() );
            if ( mapped != CertificateStoreError::NotFound )
            {
                ConsensusManagerLogger()->critical( "{}: certificate read failed key={} error={}",
                                                    __func__,
                                                    slot_key,
                                                    certificate_data_result.error().message() );
            }
            return outcome::failure( mapped );
        }
        const auto &certificate_data = certificate_data_result.value();
        const auto  stored_bytes     = std::string( certificate_data.toString() );

        Certificate certificate;
        if ( !certificate.ParseFromArray( certificate_data.data(), certificate_data.size() ) )
        {
            ConsensusManagerLogger()->critical( "{}: invalid certificate payload key={}", __func__, slot_key );
            return outcome::failure( CertificateStoreError::IntegrityError );
        }

        auto normalized = NormalizeCertificateStructural( certificate );
        if ( normalized.check != Check::Approve || normalized.deterministic_bytes != stored_bytes )
        {
            ConsensusManagerLogger()->critical( "{}: noncanonical authoritative certificate payload key={}",
                                                __func__,
                                                slot_key );
            return outcome::failure( CertificateStoreError::IntegrityError );
        }
        certificate = std::move( normalized.certificate );

        auto certificate_slot = GetSlotKey( certificate.proposal() );
        if ( certificate_slot.has_error() || certificate_slot.value() != slot_id )
        {
            ConsensusManagerLogger()->critical( "{}: certificate stored beneath wrong slot key={} actual_slot={}",
                                                __func__,
                                                slot_key,
                                                certificate_slot.has_value() ? certificate_slot.value() : "<invalid>" );
            return outcome::failure( CertificateStoreError::IntegrityError );
        }
        return certificate;
    }

    outcome::result<ConsensusManager::Certificate> ConsensusManager::GetCertificateBySubjectHash(
        const std::string &subject_hash ) const
    {
        if ( !IsCanonicalHash( subject_hash ) )
        {
            return outcome::failure( CertificateStoreError::InvalidInput );
        }

        const auto index_key = std::string{ CERTIFICATE_TX_INDEX_BASE_PATH_KEY } + subject_hash;
        auto       slot_data_result = certificate_record_reader_( { index_key } );
        if ( slot_data_result.has_error() )
        {
            const auto mapped = MapCertificateReadError( slot_data_result.error() );
            if ( mapped != CertificateStoreError::NotFound )
            {
                ConsensusManagerLogger()->critical( "{}: certificate index read failed key={} error={}",
                                                    __func__,
                                                    index_key,
                                                    slot_data_result.error().message() );
            }
            return outcome::failure( mapped );
        }
        const auto slot = std::string( slot_data_result.value().toString() );
        if ( !IsCanonicalHash( slot ) )
        {
            ConsensusManagerLogger()->critical(
                "{}: corrupt certificate index key={} slot={}", __func__, index_key, slot );
            return outcome::failure( CertificateStoreError::IntegrityError );
        }

        auto certificate_result = GetCertificateBySlotId( slot );
        if ( certificate_result.has_error() )
        {
            if ( certificate_result.error() == make_error_code( CertificateStoreError::NotFound ) )
            {
                ConsensusManagerLogger()->critical(
                    "{}: certificate index targets missing authoritative slot index_key={} slot={}",
                    __func__,
                    index_key,
                    slot );
                return outcome::failure( CertificateStoreError::IntegrityError );
            }
            return outcome::failure( certificate_result.error() );
        }

        auto current_hash = GetSubjectHash( certificate_result.value().proposal().subject() );
        if ( current_hash.has_error() || current_hash.value() != subject_hash )
        {
            ConsensusManagerLogger()->critical(
                "{}: corrupt certificate pair index_key={} slot={} expected_winner={} actual_winner={}",
                __func__,
                index_key,
                slot,
                subject_hash,
                current_hash.has_value() ? current_hash.value() : "<invalid>" );
            return outcome::failure( CertificateStoreError::IntegrityError );
        }
        return certificate_result.value();
    }

    bool ConsensusManager::CheckCertificateForSubject( const std::string &subject_hash ) const
    {
        auto certificate_result = GetCertificateBySubjectHash( subject_hash );
        if ( certificate_result.has_error() )
        {
            if ( certificate_result.error() != make_error_code( CertificateStoreError::NotFound ) )
            {
                ConsensusManagerLogger()->critical( "{}: certificate hash lookup failed closed hash={} error={}",
                                                    __func__,
                                                    subject_hash,
                                                    certificate_result.error().message() );
            }
            return false;
        }
        return true;
    }

    bool ConsensusManager::CheckCertificateForSubject( const ConsensusManager::Subject &subject ) const
    {
        auto slot_result = GetSlotKey( subject );
        if ( slot_result.has_error() )
        {
            ConsensusManagerLogger()->error( "{}: failed to derive canonical subject slot hash={} error={}",
                                             __func__,
                                             GetPrintableSubjectHash( subject ),
                                             slot_result.error().message() );
            return false;
        }

        auto certificate_result = GetCertificateBySlotId( slot_result.value() );
        if ( certificate_result.has_error() )
        {
            if ( certificate_result.error() != make_error_code( CertificateStoreError::NotFound ) )
            {
                ConsensusManagerLogger()->critical(
                    "{}: authoritative slot lookup failed closed slot={} subject={} error={}",
                    __func__,
                    slot_result.value(),
                    GetPrintableSubjectHash( subject ),
                    certificate_result.error().message() );
            }
            return false;
        }

        auto winner_hash = GetSubjectHash( certificate_result.value().proposal().subject() );
        ConsensusManagerLogger()->debug( "{}: finalized slot={} candidate={} winner={}",
                                         __func__,
                                         slot_result.value(),
                                         GetPrintableSubjectHash( subject ),
                                         winner_hash.has_value() ? winner_hash.value() : "<invalid>" );
        return true;
    }

    std::string ConsensusManager::GetPrintableSubjectHash( const Subject &subject )
    {
        auto              subject_hash = GetSubjectHash( subject );
        const std::string short_hash   = subject_hash.has_value() ? subject_hash.value().substr( 0, 8 ) : "Invalid";
        return short_hash;
    }

}
