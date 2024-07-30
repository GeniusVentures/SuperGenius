
#ifndef SUPERGENIUS_SRC_API_CHAIN_API_HPP
#define SUPERGENIUS_SRC_API_CHAIN_API_HPP

#include <boost/variant.hpp>
#include "base/buffer.hpp"
#include "outcome/outcome.hpp"
#include "primitives/common.hpp"
#include "singleton/IComponent.hpp"

namespace sgns::api {
  /**
   * @class ChainApi privides interface for blockchain api
   */
  class ChainApi : public IComponent {
   public:
       ~ChainApi() override = default;
    using BlockNumber = primitives::BlockNumber;
    using BlockHash = sgns::primitives::BlockHash;
    using ValueType = boost::variant<BlockNumber, std::string>;

    /**
     * @return last finalized block hash
     */
    virtual outcome::result<BlockHash> getBlockHash() const = 0;

    /**
     * @param block_number block number
     * @return block hash by number
     */
    virtual outcome::result<BlockHash> getBlockHash(
        BlockNumber block_number) const = 0;

    /**
     * @param hex_number hex-encoded block number
     * @return block hash by number
     */
    virtual outcome::result<BlockHash> getBlockHash(
        std::string_view hex_number) const = 0;

    /**
     * @param values mixed values array either of block number of hex-encoded
     * block number as string
     * @return array of block hashes for numbers
     */
    virtual outcome::result<std::vector<BlockHash>> getBlockHash(
        gsl::span<const ValueType> values) const = 0;
  };

}  // namespace sgns::api

#endif  // SUPERGENIUS_SRC_API_CHAIN_API_HPP
