

#ifndef SUPERGENIUS_ROUTER_HPP
#define SUPERGENIUS_ROUTER_HPP

#include <libp2p/connection/stream.hpp>
#include "singleton/IComponent.hpp"

namespace sgns::network {
  /**
   * Router, which reads and delivers different network messages to the
   * observers, responsible for their processing
   */
  class Router : public IComponent {
   protected:
    using Stream = libp2p::connection::Stream;

   public:
       ~Router() override = default;

    /**
     * Start accepting new connections and messages on this router
     */
    virtual void init() = 0;

    /**
     * Handle stream, which is opened over a Sync protocol
     * @param stream to be handled
     */
    virtual void handleSyncProtocol( std::shared_ptr<Stream> stream ) const = 0;

    /**
     * Handle stream, which is opened over a Gossip protocol
     * @param stream to be handled
     */
    virtual void handleGossipProtocol(std::shared_ptr<Stream> stream) const = 0;
  };
}  // namespace sgns::network

#endif  // SUPERGENIUS_ROUTER_HPP
