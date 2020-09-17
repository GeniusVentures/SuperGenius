#ifndef SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_FINALITY_VOTE_GRAPH_MOCK_HPP
#define SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_FINALITY_VOTE_GRAPH_MOCK_HPP

#include "verification/finality/vote_graph.hpp"

#include <gmock/gmock.h>

namespace sgns::verification::finality {

  class VoteGraphMock : public VoteGraph {
   public:
    MOCK_CONST_METHOD0(getBase, const BlockInfo &());
    MOCK_METHOD1(adjustBase, void(const std::vector<BlockHash> &));
    MOCK_METHOD2(insert,
                 outcome::result<void>(const BlockInfo &block,
                                       const VoteWeight &vote));

    MOCK_METHOD2(insert,
                 outcome::result<void>(const Prevote &prevote,
                                       const VoteWeight &vote));

    MOCK_METHOD2(insert,
                 outcome::result<void>(const Precommit &precommit,
                                       const VoteWeight &vote));

    MOCK_METHOD2(findAncestor,
                 boost::optional<BlockInfo>(const BlockInfo &,
                                            const Condition &));
    MOCK_METHOD2(findGhost,
                 boost::optional<BlockInfo>(const boost::optional<BlockInfo> &,
                                            const Condition &));
  };

}  // namespace sgns::verification::finality

#endif  // SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_FINALITY_VOTE_GRAPH_MOCK_HPP
