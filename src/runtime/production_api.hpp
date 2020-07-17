

#ifndef SUPERGENIUS_SRC_RUNTIME_PRODUCTION_API_HPP
#define SUPERGENIUS_SRC_RUNTIME_PRODUCTION_API_HPP

#include <outcome/outcome.hpp>

#include "primitives/production_configuration.hpp"
#include "primitives/block_id.hpp"

namespace sgns::runtime {

  /**
   * Api to invoke runtime entries related for Production algorithm
   */
  class ProductionApi {
   public:
    virtual ~ProductionApi() = default;

    /**
     * Get configuration for the babe
     * @return Production configuration
     */
    virtual outcome::result<primitives::ProductionConfiguration> configuration() = 0;
  };

}  // namespace sgns::runtime

#endif  // SUPERGENIUS_SRC_RUNTIME_PRODUCTION_API_HPP
