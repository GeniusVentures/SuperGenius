#ifndef SUPERGENIUS_TESTUTIL_GLOBALDB_NODE_SHUTDOWN_HPP
#define SUPERGENIUS_TESTUTIL_GLOBALDB_NODE_SHUTDOWN_HPP

#include <memory>

#include <boost/asio/io_context.hpp>
#include <ipfs_pubsub/gossip_pubsub.hpp>

#include "crdt/globaldb/globaldb.hpp"

namespace sgns::test
{
    inline void stopGlobalDBNode( std::shared_ptr<crdt::GlobalDB> &db,
                                  const std::shared_ptr<ipfs_pubsub::GossipPubSub> &pubsub )
    {
        // PubSub drops its context in Stop(), but GraphSync retains host sockets
        // until the database is released. Keep their reactor alive through both.
        const auto contextKeepalive = pubsub ? pubsub->GetAsioContext() : nullptr;
        if ( db )
        {
            // Drain CRDT workers before PubSub releases its publishing state.
            db->ShutdownNow();
        }
        if ( pubsub )
        {
            // Late stream completions borrow GraphSync's scheduler. Retain the
            // database (and scheduler) until the networking thread has joined.
            pubsub->Stop();
        }
        db.reset();
    }
} // namespace sgns::test

#endif // SUPERGENIUS_TESTUTIL_GLOBALDB_NODE_SHUTDOWN_HPP
