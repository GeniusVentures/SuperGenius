
#ifndef SUPERGENIUS_VOTE_TRACKER_IMPL_HPP
#define SUPERGENIUS_VOTE_TRACKER_IMPL_HPP

#include "verification/finality/vote_tracker.hpp"
#include <boost/operators.hpp>

namespace sgns::verification::finality {

  struct VoteOrderComparator {
    using VotingMessage = typename VoteTracker::VotingMessage;	  

    bool operator()(const Timestamp ts1, const Timestamp ts2) const {
      return ts1 < ts2;
    }
  };

  class VoteTrackerImpl : public VoteTracker {
   public:
    using PushResult = typename VoteTracker::PushResult;
    using VotingMessage = typename VoteTracker::VotingMessage;
    using EquivocatoryVotingMessage =
        typename VoteTracker::EquivocatoryVotingMessage;
    using VoteVariant = typename VoteTracker::VoteVariant;
    using BlockHash = typename VoteTracker::BlockHash;

    ~VoteTrackerImpl() override = default;

    PushResult push(const VotingMessage &vote, size_t weight) override;

    std::vector<VoteVariant> getMessages() const override;

    size_t getTotalWeight() const override;

    VotingMessage& getMedianMessage(const BlockHash& block_hash) const; 

   private:
    std::map<Id, VoteVariant> messages_;
    // ordered votes
    std::map<Timestamp, VotingMessage, VoteOrderComparator> ordered_votes_;
    size_t total_weight_ = 0;
  };

}  // namespace sgns::verification::finality

#endif  // SUPERGENIUS_VOTE_TRACKER_IMPL_HPP
