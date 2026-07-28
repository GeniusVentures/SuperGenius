#ifndef SUPERGENIUS_CONSENSUS_STATE_STORE_HPP
#define SUPERGENIUS_CONSENSUS_STATE_STORE_HPP

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "blockchain/impl/proto/ConsensusLocalState.pb.h"
#include "outcome/outcome.hpp"
#include "storage/face/write_batch.hpp"
#include "storage/rocksdb/rocksdb.hpp"

namespace sgns
{
    class ConsensusManager;
    class ConsensusBurnReservationTestAccess;
    class ConsensusVoteJournalTestAccess;

    enum class ConsensusStateStoreError : uint8_t
    {
        InvalidArgument,
        Integrity,
        Conflict,
        DatastoreIdentity,
        Storage,
    };

    class ConsensusStateStore
    {
    public:
        using VoteRecord     = local_consensus::DurableVoteRecord;
        using ProcessRecord  = local_consensus::CertificateProcessingRecord;
        using ConflictRecord = local_consensus::CertificateConflictRecord;
        using SafetyRecord   = local_consensus::SlotSafetyRecord;
        using BurnReservationRecord = local_consensus::BurnReservationRecord;
        using BurnOutpointIndex = local_consensus::BurnReservationOutpointIndex;

        struct BurnOutpoint
        {
            std::string source_chain;
            std::string burn_hash;
            uint32_t    receipt_log_index{ 0 };
        };

        struct BurnReservationResult
        {
            BurnReservationRecord record;
            bool                  created{ false };
        };

        enum class BurnDeleteResult : uint8_t
        {
            Deleted,
            NotFound,
            GenerationMismatch,
        };

        /** Exact certificate-bound identity required at the finalized batch boundary. */
        struct FinalizedReservationIdentity
        {
            std::string  slot_id;
            BurnOutpoint outpoint;
            std::string  generation;
            std::string  certificate_digest;
            std::string  proposal_id;
            std::string  winner_id;
        };

        /**
         * The single finalized-reservation serialization gate. Participant lock
         * order is store gate -> UTXO persistence -> UTXO state. Participants
         * must not acquire proposal, restored-state, handler-registry, or a
         * second store gate, and must not invoke external callbacks. The
         * participant owns staging and committing the supplied batch.
         */
        using FinalizedBatchParticipant =
            std::function<outcome::result<void>( storage::BufferBatch &, const BurnReservationRecord & )>;

        explicit ConsensusStateStore( std::shared_ptr<storage::rocksdb> datastore );

        outcome::result<std::optional<VoteRecord>> GetVote( const std::string &validator_id,
                                                            const std::string &slot_id ) const;
        outcome::result<std::vector<VoteRecord>>   ScanVotes() const;
        outcome::result<void> PutActiveVote( const VoteRecord &record );
        outcome::result<void> UpdatePublication( const std::string &validator_id,
                                                 const std::string &slot_id,
                                                 uint64_t           published_at_ms,
                                                 bool               succeeded );
        outcome::result<void> RetireVote( const std::string &validator_id,
                                          const std::string &slot_id,
                                          uint64_t           retired_at_ms );

        outcome::result<std::optional<ProcessRecord>> GetProcess( const std::string &slot_id ) const;
        outcome::result<std::vector<ProcessRecord>>   ScanProcesses() const;
        outcome::result<void> PutPendingProcess( const ProcessRecord &record );
        outcome::result<void> MarkProcessing( const std::string &slot_id,
                                              uint64_t           lease_until_ms,
                                              uint64_t           updated_at_ms );
        outcome::result<void> RestorePending( const std::string &slot_id, uint64_t updated_at_ms );
        outcome::result<void> MarkComplete( const std::string &slot_id, uint64_t updated_at_ms );

        outcome::result<std::vector<ConflictRecord>> ScanConflicts() const;
        outcome::result<std::vector<SafetyRecord>>   ScanSafety() const;
        outcome::result<ConflictRecord> RecordConflictAndSafety( ConflictRecord conflict, SafetyRecord safety );

        outcome::result<std::optional<BurnReservationRecord>> GetBurnReservation( const std::string &slot_id ) const;
        outcome::result<std::optional<BurnReservationRecord>> GetBurnReservation(
            const BurnOutpoint &outpoint ) const;
        outcome::result<std::vector<BurnReservationRecord>> ScanBurnReservations() const;
        outcome::result<BurnReservationResult> CreateOrJoinBurnReservation(
            const std::string &slot_id,
            const BurnOutpoint &outpoint,
            uint64_t candidate_acceptance_horizon_ms,
            uint64_t now_ms );
        outcome::result<BurnReservationRecord> FinalizeBurnReservation(
            const std::string &slot_id,
            const BurnOutpoint &outpoint,
            const std::string &certificate_digest,
            const std::string &proposal_id,
            const std::string &winner_id,
            uint64_t now_ms );
        outcome::result<BurnReservationRecord> MarkBurnReservationSafetyError(
            const std::string &slot_id,
            const std::string &expected_generation,
            const std::string &certificate_digest,
            const std::string &proposal_id,
            const std::string &winner_id,
            const std::string &diagnostic,
            uint64_t now_ms );
        outcome::result<BurnReservationRecord> PrepareConsumedBurnReservation(
            storage::BufferBatch &batch,
            const std::string &slot_id,
            const BurnOutpoint &outpoint,
            const std::string &expected_generation,
            const std::string &certificate_digest,
            const std::string &proposal_id,
            const std::string &winner_id,
            uint64_t now_ms );
        outcome::result<BurnDeleteResult> DeleteReservedBurnReservation(
            const std::string &slot_id,
            const std::string &expected_generation,
            std::optional<uint64_t> expected_candidate_horizon_ms = std::nullopt );
        outcome::result<void> ApplyFinalizedReservationBatch(
            const FinalizedReservationIdentity &identity,
            const std::shared_ptr<storage::rocksdb> &participant_datastore,
            FinalizedBatchParticipant participant );

        static std::string VoteKey( const std::string &validator_id, const std::string &slot_id );
        static std::string ProcessKey( const std::string &slot_id );
        static std::string ConflictKey( const std::string &slot_id,
                                        const std::string &low_digest,
                                        const std::string &high_digest );
        static std::string SafetyKey( const std::string &slot_id );
        static std::string BurnSlotKey( const std::string &slot_id );
        static std::string BurnOutpointKey( const BurnOutpoint &outpoint );

    private:
        using QueryFn = std::function<outcome::result<storage::rocksdb::QueryResult>( const base::Buffer & )>;
        using CommitFn = std::function<outcome::result<void>( storage::BufferBatch & )>;

        outcome::result<std::optional<VoteRecord>> ReadVoteUnlocked( const std::string &validator_id,
                                                                    const std::string &slot_id ) const;
        outcome::result<std::optional<ProcessRecord>> ReadProcessUnlocked( const std::string &slot_id ) const;
        outcome::result<void> ValidateVote( const VoteRecord &record, const std::string &key ) const;
        outcome::result<void> ValidateProcess( const ProcessRecord &record, const std::string &key ) const;
        outcome::result<void> ValidateConflict( const ConflictRecord &record, const std::string &key ) const;
        outcome::result<void> ValidateSafety( const SafetyRecord &record, const std::string &key ) const;
        outcome::result<std::optional<BurnReservationRecord>> ReadBurnReservationUnlocked(
            const std::string &slot_id ) const;
        outcome::result<std::optional<BurnOutpointIndex>> ReadBurnOutpointIndexUnlocked(
            const BurnOutpoint &outpoint ) const;
        outcome::result<void> ValidateBurnReservation( const BurnReservationRecord &record,
                                                       const std::string &key ) const;
        outcome::result<void> ValidateBurnOutpointIndex( const BurnOutpointIndex &record,
                                                         const std::string &key ) const;
        outcome::result<void> ValidateBurnReciprocalUnlocked( const BurnReservationRecord &record ) const;
        outcome::result<BurnReservationRecord> PrepareConsumedBurnReservationUnlocked(
            storage::BufferBatch &batch,
            const FinalizedReservationIdentity &identity,
            uint64_t now_ms );

        std::shared_ptr<storage::rocksdb> datastore_;
        mutable std::mutex                mutex_;
        QueryFn                          query_;
        CommitFn                         commit_;

        friend class ConsensusVoteJournalTestAccess;
        friend class ConsensusBurnReservationTestAccess;
        friend class ConsensusManager;
    };
} // namespace sgns

OUTCOME_HPP_DECLARE_ERROR_2( sgns, ConsensusStateStoreError );

#endif // SUPERGENIUS_CONSENSUS_STATE_STORE_HPP
