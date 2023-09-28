

#include "verification/finality/impl/vote_tracker_impl.hpp"

#include "base/visitor.hpp"
#include "verification/finality/structs.hpp"

namespace sgns::verification::finality {

  using VotingMessage = VoteTracker::VotingMessage;

  VoteTracker::PushResult VoteTrackerImpl::push(const VotingMessage &vote,
                                                size_t weight) {
    auto vote_it = messages_.find(vote.id);
    if (vote_it == messages_.end()) {
      messages_[vote.id] = {vote};
      total_weight_ += weight;

      // ordered votes
      ordered_votes_[vote.ts] = {vote};
      return PushResult::SUCCESS;
    }
    auto &equivotes = vote_it->second;
    bool isDuplicate = visit_in_place(
        equivotes,
        [&vote](const VotingMessage &voting_message) {
          return voting_message.block_hash() == vote.block_hash();
        },
        [&vote](const EquivocatoryVotingMessage &equivocatory_vote) {
          return equivocatory_vote.first.block_hash() == vote.block_hash()
                 || equivocatory_vote.second.block_hash() == vote.block_hash();
        });
    if (! isDuplicate) {
      return visit_in_place(
          equivotes,
          // if there is only single vote for that id, make it equivocatory vote
          [&](const VotingMessage &voting_message) {
            EquivocatoryVotingMessage v;
            v.first = boost::get<VotingMessage>(equivotes);
            v.second = vote;

            messages_[vote.id] = v;
            total_weight_ += weight;
            return PushResult::EQUIVOCATED;
          },
          // otherwise return duplicated
          [&](const EquivocatoryVotingMessage &) {
            return PushResult::DUPLICATED;
          });
    }
    return PushResult::DUPLICATED;
  }

  std::vector<typename VoteTracker::VoteVariant> VoteTrackerImpl::getMessages()
      const {
    std::vector<typename VoteTracker::VoteVariant> prevotes;
    // the actual number may be bigger, but it's a good guess
    prevotes.reserve(messages_.size());
    for (const auto &[key, value] : messages_) {
      prevotes.push_back(value);
    }
    return prevotes;
  }

  VotingMessage VoteTrackerImpl::getMedianMessage(const BlockHash& block_hash)
       const {
     std::vector<VotingMessage> tmp_votes;
     for (const auto &item : ordered_votes_) {
       if(block_hash == item.second.block_hash()) {
         tmp_votes.push_back(item.second);
       }
     }	     
     size_t size = tmp_votes.size();
     size_t median_index = size % 2 == 0 ? (size/2) - 1 : size/2;
     assert((median_index >= 0) && (median_index < size));
     return tmp_votes[median_index];
  }	  

  size_t VoteTrackerImpl::getTotalWeight() const {
    return total_weight_;
  }

}  // namespace sgns::verification::finality
