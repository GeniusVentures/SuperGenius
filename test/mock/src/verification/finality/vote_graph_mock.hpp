#ifndef SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_FINALITY_VOTE_GRAPH_MOCK_HPP
#define SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_FINALITY_VOTE_GRAPH_MOCK_HPP

#include "verification/finality/vote_graph/vote_graph_impl.hpp"

#include <gmock/gmock.h>

namespace sgns::verification::finality {

  class VoteGraphMock : public VoteGraphImpl {
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

    MOCK_METHOD3(findAncestor,
                 boost::optional<BlockInfo>(const BlockInfo &,
                                            const Condition &,
					    const Comparator &));
    MOCK_METHOD3(findGhost,
                 boost::optional<BlockInfo>(const boost::optional<BlockInfo> &,
                                            const Condition &,
					    const Comparator &));
  };

}  // namespace sgns::verification::finality

#endif  // SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_FINALITY_VOTE_GRAPH_MOCK_HPP
