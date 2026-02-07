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
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

#include "blockchain/ValidatorRegistry.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"
#include "ipfs_pubsub/gossip_pubsub.hpp"
#include "outcome/outcome.hpp"

namespace sgns
{
    class ConsensusManager : public std::enable_shared_from_this<ConsensusManager>
    {
    public:
        using Proposal    = ConsensusProposal;
        using Vote        = ConsensusVote;
        using VoteBundle  = ConsensusVoteBundle;
        using Certificate = ConsensusCertificate;
        using Subject     = ConsensusSubject;

        using Signer             = std::function<outcome::result<std::vector<uint8_t>>( std::vector<uint8_t> payload )>;
        using ProposalHandler    = std::function<void( const Proposal &proposal )>;
        using VoteHandler        = std::function<void( const Vote &vote )>;
        using VoteBundleHandler  = std::function<void( const VoteBundle &bundle )>;
        using CertificateHandler = std::function<void( const Certificate &certificate )>;
        using CertificateCallback = std::function<void( const Proposal &proposal, const Certificate &certificate )>;
        using ProposalValidator   = std::function<bool( const Proposal &proposal )>;
        enum class SubjectCheck
        {
            Approve,
            Reject,
            Pending
        };
        using SubjectHandler = std::function<outcome::result<SubjectCheck>( const Subject &subject )>;

        struct QuorumTally
        {
            uint64_t total_weight    = 0;
            uint64_t approved_weight = 0;
            bool     has_quorum      = false;
        };

        static std::shared_ptr<ConsensusManager> New( std::shared_ptr<ValidatorRegistry>         registry,
                                                      std::shared_ptr<ipfs_pubsub::GossipPubSub> pubsub,
                                                      Signer                                     signer,
                                                      std::string                                address,
                                                      std::string                                consensus_topic = "" );

        void SetProposalValidator( ProposalValidator validator );
        bool RegisterSubjectHandler( SubjectType type, SubjectHandler handler );
        void UnregisterSubjectHandler( SubjectType type );
        void SetCertificateCallback( CertificateCallback callback );

        outcome::result<void> Publish( const ConsensusMessage &message );

        void SetProposalHandler( ProposalHandler handler );
        void SetVoteHandler( VoteHandler handler );
        void SetVoteBundleHandler( VoteBundleHandler handler );
        void SetCertificateHandler( CertificateHandler handler );

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

        outcome::result<QuorumTally> TallyVotes( const Proposal &proposal, const std::vector<Vote> &votes );

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
        outcome::result<void>                        SubmitVote( const Vote &vote );
        outcome::result<void>                        SubmitCertificate( const Certificate &certificate );
        outcome::result<void>                        ResumeProposalHandling( const std::string &subject_hash );

    protected:
        void ConfigureTimestampWindow( std::chrono::milliseconds window );

    private:
        explicit ConsensusManager( std::shared_ptr<ValidatorRegistry>         registry,
                                   std::shared_ptr<ipfs_pubsub::GossipPubSub> pubsub,
                                   Signer                                     signer,
                                   std::string                                address,
                                   std::string                                consensus_topic );

        static constexpr std::string_view          CONSENSUS_CHANNEL_PREFIX = "consensus-channel-";
        static constexpr std::chrono::milliseconds DEFAULT_TIMESTAMP_WINDOW = std::chrono::minutes( 5 );

        struct ProposalState
        {
            Proposal                   proposal;
            std::vector<Vote>          votes;
            std::optional<Certificate> certificate;
            std::string                slot_key;
            uint64_t                   total_weight    = 0;
            uint64_t                   approved_weight = 0;
            std::unordered_set<std::string> seen_voters;
        };

        struct SlotState
        {
            std::string best_proposal_id;
            std::string best_tx_hash;
            bool        voted = false;
        };

        void                         HandleProposal( const Proposal &proposal );
        void                         HandleVote( const Vote &vote );
        void                         HandleVoteBundle( const VoteBundle &bundle );
        void                         HandleCertificate( const Certificate &certificate );
        void                         NotifyCertificate( const Proposal &proposal, const Certificate &certificate );
        std::string                  GetSlotKey( const Proposal &proposal ) const;
        bool                         IsBetterProposal( const Proposal &candidate, const Proposal &current ) const;
        bool                         IsTimestampSane( uint64_t timestamp_ms ) const;
        outcome::result<std::string> GetSubjectHash( const Subject &subject ) const;
        void                         ContinueProposalAfterSubject( const Proposal &proposal );
        void                         AddPendingProposal( const Proposal &proposal, const std::string &subject_hash );
        std::vector<Proposal>        TakePendingProposals( const std::string &subject_hash );

        static std::string                       CreateProposalId( const Proposal &proposal );
        static bool                              ValidateSubject( const Subject &subject );
        const ValidatorRegistry::ValidatorEntry *FindValidator( const ValidatorRegistry::Registry &registry,
                                                                const std::string                 &validator_id ) const;

        void OnConsensusMessage( boost::optional<const ipfs_pubsub::GossipPubSub::Message &> message );
        bool CheckSubject( const Subject &subject );

        std::shared_ptr<ValidatorRegistry>                        registry_;
        VoteHandler                                               vote_handler_;
        VoteBundleHandler                                         vote_bundle_handler_;
        CertificateHandler                                        certificate_handler_;
        CertificateCallback                                       certificate_callback_;
        ProposalValidator                                         proposal_validator_;
        std::unordered_map<int, SubjectHandler>                   subject_handlers_;
        mutable std::shared_mutex                                 subject_handlers_mutex_;
        Signer                                                    signer_;
        std::string                                               account_address_;
        std::unordered_map<std::string, ProposalState>            proposals_;
        std::unordered_map<std::string, SlotState>                slot_states_;
        std::unordered_map<std::string, Proposal>                 pending_proposals_;
        std::unordered_map<std::string, std::vector<std::string>> pending_by_subject_hash_;
        mutable std::mutex                                        proposals_mutex_;
        std::shared_ptr<ipfs_pubsub::GossipPubSub>                pubsub_;

        std::string                                                         consensus_topic_;
        std::shared_future<std::shared_ptr<libp2p::protocol::Subscription>> consensus_subs_future_;
        std::chrono::milliseconds timestamp_window_{ DEFAULT_TIMESTAMP_WINDOW };
    };
}
