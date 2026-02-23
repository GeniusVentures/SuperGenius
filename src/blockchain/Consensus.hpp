/**
 * @file       Consensus.hpp
 * @brief      Consensus proposal/vote/certificate helpers.
 * @date       2025-10-16
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <shared_mutex>
#include <thread>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <limits>

#include "blockchain/ValidatorRegistry.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/proto/delta.pb.h"
#include "ipfs_pubsub/gossip_pubsub.hpp"
#include "outcome/outcome.hpp"

namespace sgns
{
    class ConsensusManager : public std::enable_shared_from_this<ConsensusManager>
    {
    public:
        ~ConsensusManager();
        void Close();
        using Proposal    = ConsensusProposal;
        using Vote        = ConsensusVote;
        using VoteBundle  = ConsensusVoteBundle;
        using Certificate = ConsensusCertificate;
        using Subject     = ConsensusSubject;

        using Signer = std::function<outcome::result<std::vector<uint8_t>>( std::vector<uint8_t> payload )>;
        enum class SubjectCheck
        {
            Approve,
            Reject,
            Pending
        };
        using SubjectHandler = std::function<outcome::result<SubjectCheck>( const Subject &subject )>;
        using CertificateSubjectHandler =
            std::function<void( const std::string &subject_hash, const Certificate &certificate )>;

        struct QuorumTally
        {
            uint64_t total_weight    = 0;
            uint64_t approved_weight = 0;
            bool     has_quorum      = false;
        };

        static std::shared_ptr<ConsensusManager> New( std::shared_ptr<ValidatorRegistry>         registry,
                                                      std::shared_ptr<crdt::GlobalDB>            db,
                                                      std::shared_ptr<ipfs_pubsub::GossipPubSub> pubsub,
                                                      Signer                                     signer,
                                                      std::string                                address,
                                                      std::string                                consensus_topic = "" );

        bool RegisterSubjectHandler( SubjectType type, SubjectHandler handler );
        void UnregisterSubjectHandler( SubjectType type );
        bool RegisterCertificateHandler( SubjectType type, CertificateSubjectHandler handler );
        void UnregisterCertificateHandler( SubjectType type );

        outcome::result<void> Publish( const ConsensusMessage &message );

        outcome::result<Proposal>        CreateProposal( const Subject     &subject,
                                                         const std::string &proposer_id,
                                                         const std::string &registry_cid,
                                                         uint64_t           registry_epoch );
        static outcome::result<Proposal> CreateProposal( const Subject     &subject,
                                                         const std::string &proposer_id,
                                                         const std::string &registry_cid,
                                                         uint64_t           registry_epoch,
                                                         Signer             sign );

        outcome::result<Vote> CreateVote( const std::string &proposal_id,
                                          const std::string &voter_id,
                                          bool               approve,
                                          Signer             sign );

        outcome::result<VoteBundle> CreateVoteBundle( const std::string       &proposal_id,
                                                      const std::string       &aggregator_id,
                                                      const std::vector<Vote> &votes,
                                                      Signer                   sign );

        outcome::result<Certificate> CreateCertificate( const Proposal &proposal, const std::vector<Vote> &votes );

        outcome::result<QuorumTally> TallyVotes( const Proposal                    &proposal,
                                                 const std::vector<Vote>           &votes,
                                                 const ValidatorRegistry::Registry &registry,
                                                 const std::string                 &registry_cid ) const;
        outcome::result<QuorumTally> TallyVotes( const Proposal &proposal, const std::vector<Vote> &votes ) const;

        static outcome::result<std::vector<uint8_t>> ProposalSigningBytes( const Proposal &proposal );
        static outcome::result<std::vector<uint8_t>> VoteSigningBytes( const Vote &vote );
        static outcome::result<std::vector<uint8_t>> VoteBundleSigningBytes( const VoteBundle &bundle );
        static outcome::result<std::string>          ComputeSubjectId( const Subject &subject );
        static outcome::result<Subject>              CreateNonceSubject( const std::string &account_id,
                                                                         uint64_t           nonce,
                                                                         const std::string &tx_hash );
        static outcome::result<Subject>              CreateTaskResultSubject( const std::string &account_id,
                                                                              const std::string &escrow_path,
                                                                              const std::string &task_result_hash,
                                                                              uint64_t           result_epoch );
        outcome::result<void>                        SubmitProposal( const Proposal &proposal, bool self_vote = true );
        outcome::result<void>                        SubmitVote( const Vote &vote, bool self_handle = true );
        outcome::result<void>                        SubmitCertificate( const Certificate &certificate );
        outcome::result<void>                        ResumeProposalHandling( const std::string &subject_hash );
        void                                         ProcessCertificates();

        outcome::result<Certificate> GetCertificateBySubjectHash( const std::string &subject_hash ) const;
        bool CheckCertificateForSubject( const std::string &subject_hash ) const;


    protected:
        void ConfigureTimestampWindow( std::chrono::milliseconds window );
        void ConfigureRoundDuration( std::chrono::milliseconds duration );
        void ConfigureRoundSkew( std::chrono::milliseconds skew );

    private:
        explicit ConsensusManager( std::shared_ptr<ValidatorRegistry>         registry,
                                   std::shared_ptr<crdt::GlobalDB>            db,
                                   std::shared_ptr<ipfs_pubsub::GossipPubSub> pubsub,
                                   Signer                                     signer,
                                   std::string                                address,
                                   std::string                                consensus_topic );
        void StartRoundTimer();

        static constexpr std::string_view          CONSENSUS_CHANNEL_PREFIX  = "consensus-channel-";
        static constexpr std::string_view          CERTIFICATE_BASE_PATH_KEY = "/cert/";
        static constexpr std::chrono::milliseconds DEFAULT_TIMESTAMP_WINDOW  = std::chrono::minutes( 5 );
        static constexpr std::chrono::milliseconds DEFAULT_ROUND_DURATION    = std::chrono::milliseconds( 500 );
        static constexpr std::chrono::milliseconds DEFAULT_ROUND_SKEW        = std::chrono::milliseconds( 250 );
        static constexpr uint64_t                 NO_ROUND                   = std::numeric_limits<uint64_t>::max();

        struct ProposalState
        {
            Proposal                        proposal;
            std::vector<Vote>               votes;
            std::string                     slot_key;
            uint64_t                        total_weight    = 0;
            uint64_t                        approved_weight = 0;
            std::unordered_set<std::string> seen_voters;
            bool                            quorum_reached     = false;
            uint64_t                        last_attempt_round = NO_ROUND;
        };

        struct SlotState
        {
            std::string best_proposal_id;
            std::string best_tx_hash;
            bool        voted = false;
        };

        void        HandleProposal( const Proposal &proposal );
        void        HandleVote( const Vote &vote );
        void        HandleVoteBundle( const VoteBundle &bundle );
        void        HandleCertificate( const Certificate &certificate );
        std::string GetSlotKey( const Proposal &proposal ) const;
        bool        IsBetterProposal( const Proposal &candidate, const Proposal &current ) const;
        bool        IsTimestampSane( uint64_t timestamp_ms ) const;
        bool        IsCurrentAggregator( const Proposal &proposal, const ValidatorRegistry::Registry &registry ) const;
        std::vector<std::string>       GetOrderedActiveValidators( const ValidatorRegistry::Registry &registry ) const;
        uint64_t                       GetCurrentRound( uint64_t proposal_ts_ms ) const;
        outcome::result<ProposalState> FetchProposalState( const Certificate &certificate );
        ProposalState                  CreateProposalState( const Certificate &certificate );
        bool ValidateCertificateBestProposal( const ProposalState &state, const Certificate &certificate ) const;
        std::vector<Vote>            CollectCertificateVotes( const Certificate &certificate ) const;
        void                         ClearProposalState( const Proposal &proposal );
        outcome::result<std::string> GetSubjectHash( const Subject &subject ) const;
        void                         ContinueProposalAfterSubject( const Proposal &proposal );
        void                         AddPendingProposal( const Proposal &proposal, const std::string &subject_hash );
        std::vector<Proposal>        TakePendingProposals( const std::string &subject_hash );
        bool                         RegisterCertificateFilter();
        std::optional<std::vector<crdt::pb::Element>> FilterCertificate( const crdt::pb::Element &element );
        void CertificateReceived( crdt::CRDTCallbackManager::NewDataPair new_data, const std::string &cid );
        bool ValidateCertificate( const Certificate &certificate ) const;
        static std::string CreateProposalId( const Proposal &proposal );
        static bool        ValidateSubject( const Subject &subject );

        void        OnConsensusMessage( boost::optional<const ipfs_pubsub::GossipPubSub::Message &> message );
        void        UpdateCertificatesPending();
        static bool CheckSubject( const Subject &subject );
        static bool CheckProposal( const Proposal &proposal );
        static bool CheckVote( const Vote &vote );
        std::shared_ptr<ValidatorRegistry>                        registry_;
        std::shared_ptr<crdt::GlobalDB>                           db_;
        std::unordered_map<int, SubjectHandler>                   subject_handlers_;
        mutable std::shared_mutex                                 subject_handlers_mutex_;
        std::unordered_map<int, CertificateSubjectHandler>        certificate_subject_handlers_;
        mutable std::shared_mutex                                 certificate_handlers_mutex_;
        Signer                                                    signer_;
        std::string                                               account_address_;
        std::unordered_map<std::string, ProposalState>            proposals_;
        std::unordered_map<std::string, SlotState>                slot_states_;
        std::unordered_map<std::string, Proposal>                 pending_proposals_;
        std::unordered_map<std::string, std::vector<std::string>> pending_by_subject_hash_;
        mutable std::mutex                                        proposals_mutex_;
        std::shared_ptr<ipfs_pubsub::GossipPubSub>                pubsub_;

        std::string                                                         consensus_messages_topic_;
        std::string                                                         consensus_datastore_topic_;
        std::shared_future<std::shared_ptr<libp2p::protocol::Subscription>> consensus_subs_future_;
        std::chrono::milliseconds timestamp_window_{ DEFAULT_TIMESTAMP_WINDOW };
        std::chrono::milliseconds round_duration_{ DEFAULT_ROUND_DURATION };
        std::chrono::milliseconds round_skew_{ DEFAULT_ROUND_SKEW };
        std::atomic<bool>         stop_timer_{ false };
        std::atomic<bool>         certificates_pending_{ false };
        std::condition_variable   timer_cv_;
        std::mutex                timer_mutex_;
        std::thread               round_timer_;
    };
}
