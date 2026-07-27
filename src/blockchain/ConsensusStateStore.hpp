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
#include "storage/rocksdb/rocksdb.hpp"

namespace sgns
{
    class ConsensusManager;
    class ConsensusVoteJournalTestAccess;

    enum class ConsensusStateStoreError : uint8_t
    {
        InvalidArgument,
        Integrity,
        Conflict,
        Storage,
    };

    class ConsensusStateStore
    {
    public:
        using VoteRecord     = local_consensus::DurableVoteRecord;
        using ProcessRecord  = local_consensus::CertificateProcessingRecord;
        using ConflictRecord = local_consensus::CertificateConflictRecord;
        using SafetyRecord   = local_consensus::SlotSafetyRecord;

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

        static std::string VoteKey( const std::string &validator_id, const std::string &slot_id );
        static std::string ProcessKey( const std::string &slot_id );
        static std::string ConflictKey( const std::string &slot_id,
                                        const std::string &low_digest,
                                        const std::string &high_digest );
        static std::string SafetyKey( const std::string &slot_id );

    private:
        using QueryFn = std::function<outcome::result<storage::rocksdb::QueryResult>( const base::Buffer & )>;

        outcome::result<std::optional<VoteRecord>> ReadVoteUnlocked( const std::string &validator_id,
                                                                    const std::string &slot_id ) const;
        outcome::result<std::optional<ProcessRecord>> ReadProcessUnlocked( const std::string &slot_id ) const;
        outcome::result<void> ValidateVote( const VoteRecord &record, const std::string &key ) const;
        outcome::result<void> ValidateProcess( const ProcessRecord &record, const std::string &key ) const;
        outcome::result<void> ValidateConflict( const ConflictRecord &record, const std::string &key ) const;
        outcome::result<void> ValidateSafety( const SafetyRecord &record, const std::string &key ) const;

        std::shared_ptr<storage::rocksdb> datastore_;
        mutable std::mutex                mutex_;
        QueryFn                          query_;

        friend class ConsensusVoteJournalTestAccess;
        friend class ConsensusManager;
    };
} // namespace sgns

OUTCOME_HPP_DECLARE_ERROR_2( sgns, ConsensusStateStoreError );

#endif // SUPERGENIUS_CONSENSUS_STATE_STORE_HPP
