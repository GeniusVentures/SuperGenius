
#ifndef SUPERGENIUS_SRC_API_JRPC_JRPC_PROCESSOR_HPP
#define SUPERGENIUS_SRC_API_JRPC_JRPC_PROCESSOR_HPP

#include <boost/noncopyable.hpp>
#include "singleton/IComponent.hpp"

namespace sgns::api {
  /**
   * @class JRpcProcessor is base class for JSON RPC processors
   */
class JRpcProcessor: private boost::noncopyable, public IComponent {
   public:
       ~JRpcProcessor() override = default;

       /**
     * @brief registers callbacks for jrpc request
     */
       virtual void registerHandlers() = 0;
  };
}  // namespace sgns::api

#endif  // SUPERGENIUS_SRC_API_JRPC_JRPC_PROCESSOR_HPP
