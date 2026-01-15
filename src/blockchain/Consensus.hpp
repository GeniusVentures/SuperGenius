/**
 * @file       Consensus.hpp
 * @brief      Consensus proposal/vote/certificate helpers.
 * @date       2025-10-16
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "blockchain/ValidatorRegistry.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"
#include "outcome/outcome.hpp"

namespace sgns::blockchain
{
    class ConsensusManager
    {
    public:
        using Proposal = ConsensusProposal;
        using Vote = ConsensusVote;
        using VoteBundle = ConsensusVoteBundle;
        using Certificate = ConsensusCertificate;
        using Subject = ConsensusSubject;

        using Signer =
            std::function<std::vector<uint8_t>( std::vector<uint8_t> payload )>;
        using Verifier =
            std::function<bool( const std::string &signer_id,
                                const std::string &signature,
                                const std::vector<uint8_t> &payload )>;

        struct QuorumTally
        {
            uint64_t total_weight = 0;
            uint64_t approved_weight = 0;
            bool     has_quorum = false;
        };

        explicit ConsensusManager( std::shared_ptr<ValidatorRegistry> registry );

        void SetRegistry( std::shared_ptr<ValidatorRegistry> registry );

        outcome::result<Proposal> CreateProposal( const Subject     &subject,
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

        outcome::result<Certificate> CreateCertificate( const Proposal        &proposal,
                                                        const std::vector<Vote> &votes,
                                                        Verifier                 verify );

        outcome::result<QuorumTally> TallyVotes( const Proposal        &proposal,
                                                 const std::vector<Vote> &votes,
                                                 Verifier                 verify );

        static outcome::result<std::vector<uint8_t>> ProposalSigningBytes( const Proposal &proposal );
        static outcome::result<std::vector<uint8_t>> VoteSigningBytes( const Vote &vote );
        static outcome::result<std::vector<uint8_t>> VoteBundleSigningBytes( const VoteBundle &bundle );
        static outcome::result<std::string> ComputeSubjectId( const Subject &subject );
        static outcome::result<Subject> CreateNonceSubject( const std::string &account_id,
                                                            uint64_t           nonce,
                                                            const std::vector<uint8_t> &tx_hash );
        static outcome::result<Subject> CreateTaskResultSubject( const std::string &account_id,
                                                                 const std::string &escrow_path,
                                                                 const std::vector<uint8_t> &task_result_hash,
                                                                 uint64_t           result_epoch );

    private:
        std::string CreateProposalId( const Proposal &proposal ) const;
        bool ValidateSubject( const Subject &subject ) const;
        const ValidatorRegistry::ValidatorEntry *FindValidator(
            const ValidatorRegistry::Registry &registry,
            const std::string &validator_id ) const;

        std::shared_ptr<ValidatorRegistry> registry_;
    };
}
