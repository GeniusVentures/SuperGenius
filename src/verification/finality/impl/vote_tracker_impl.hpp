
#ifndef SUPERGENIUS_VOTE_TRACKER_IMPL_HPP
#define SUPERGENIUS_VOTE_TRACKER_IMPL_HPP

#include "verification/finality/vote_tracker.hpp"

namespace sgns::verification::finality {

  class VoteTrackerImpl : public VoteTracker {
   public:
    using PushResult = typename VoteTracker::PushResult;
    using VotingMessage = typename VoteTracker::VotingMessage;
    using EquivocatoryVotingMessage =
        typename VoteTracker::EquivocatoryVotingMessage;
    using VoteVariant = typename VoteTracker::VoteVariant;

    ~VoteTrackerImpl() override = default;

    PushResult push(const VotingMessage &vote, size_t weight) override;

    std::vector<VoteVariant> getMessages() const override;

    size_t getTotalWeight() const override;

    VotingMessage& getMedianMessage(const Vote& vote) const; 

    // order the voting messages by timestamp 
    static bool voteOrder(const VotingMessage& v1, const VotingMessage& v2) {
        return v1.ts < v2.ts; 
    } 

   private:
    std::map<Id, VoteVariant> messages_;
    // ordered votes
    std::map<Id, VotingMessage, decltype(&VoteTrackerImpl::voteOrder)> ordered_votes_(&VoteTrackerImpl::voteOrder);
    size_t total_weight_ = 0;
  };

}  // namespace sgns::verification::finality

#endif  // SUPERGENIUS_VOTE_TRACKER_IMPL_HPP
