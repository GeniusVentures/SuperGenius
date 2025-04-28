/**
 * @file       crdt_mirror_broadcaster.hpp
 * @brief      
 * @date       2025-04-22
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _CRDT_MIRROR_BROADCASTER_HPP
#define _CRDT_MIRROR_BROADCASTER_HPP

#include "crdt/broadcaster.hpp"
#include "base/buffer.hpp"
#include <queue>
#include <string>

namespace sgns::crdt
{
    /**
     * @brief Test implementation of Broadcaster for unit testing
     */
    class CRDTMirrorBroadcaster : public Broadcaster
    {
    public:
        void SetMirrorCounterPart( const std::shared_ptr<CRDTMirrorBroadcaster> &dest );
        /**
         * Send {@param buff} payload to other replicas.
         * @param buff buffer to broadcast
         * @return outcome::success on success or outcome::failure on error
         */
        outcome::result<void> Broadcast( const base::Buffer &buff, std::optional<std::string> topic_name ) override;

        /**
         * Obtain the next {@return} payload received from the network.
         * @return buffer value or outcome::failure on error
         */
        outcome::result<base::Buffer> Next() override;

        bool HasTopic( const std::string &topic ) override;

        /** Queue of broadcast messages */
        std::queue<std::string> listOfBroadcasts_;

        std::mutex mutex_;

    private:
        std::shared_ptr<CRDTMirrorBroadcaster> counterpart_;
    };

}

#endif 
