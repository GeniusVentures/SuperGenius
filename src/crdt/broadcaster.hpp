#ifndef SUPERGENIUS_BROADCASTER_HPP
#define SUPERGENIUS_BROADCASTER_HPP

#include "base/buffer.hpp"
#include <libp2p/peer/peer_info.hpp>
#include <boost/optional.hpp>
#include <tuple>
#include <string>
#include <optional>

namespace sgns::crdt
{
    /**
     * @brief A Broadcaster provides a way to send (notify) an opaque payload to
     * all replicas and to retrieve payloads broadcasted.
     */
    class Broadcaster
    {
    public:
        virtual ~Broadcaster() = default;

        enum class ErrorCode
        {
            Success            = 0, /*> 0 should not represent an error */
            ErrNoMoreBroadcast = 1, /*> no more data to broadcast */
        };

        /**
         * Send buffer payload to other replicas.
         * @param buff       Buffer containing the data to broadcast.
         * @param topic      Topic to broadcast to.
         * @param peerInfo   Optional peer info to avoid repeated GetPeerInfo calls.
         * @return outcome::success on success or outcome::failure on error.
         */
        virtual outcome::result<void> Broadcast( const base::Buffer                     &buff,
                                                 std::string                             topic,
                                                 boost::optional<libp2p::peer::PeerInfo> peerInfo = boost::none ) = 0;

        /**
         * Obtain the next payload and its topic received from the network.
         * @return buffer value or outcome::failure on error 
         */
        virtual outcome::result<base::Buffer> Next() = 0;

        /**
         * @brief Checks whether the broadcaster is subscribed to the specified topic.
         *
         * @param topic The topic string to check.
         * @return true if the broadcaster is subscribed to the topic, false otherwise.
         */
        virtual bool HasTopic( const std::string &topic ) = 0;

        /**
         * @brief Get the underlying DAG syncer (if available).
         * @return Shared pointer to the DAG syncer, or nullptr if not available.
         */
        virtual std::shared_ptr<void> GetDagSyncer() const
        {
            return nullptr;
        }
    };
}

#endif // SUPERGENIUS_BROADCASTER_HPP
