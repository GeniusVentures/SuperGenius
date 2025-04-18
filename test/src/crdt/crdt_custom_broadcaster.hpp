#ifndef SUPERGENIUS_CRDT_CUSTOM_BROADCASTER_HPP
#define SUPERGENIUS_CRDT_CUSTOM_BROADCASTER_HPP

#include "crdt/broadcaster.hpp"
#include "base/buffer.hpp"
#include <queue>
#include <string>

namespace sgns::crdt
{
    /**
     * @brief Test implementation of Broadcaster for unit testing
     */
    class CustomBroadcaster : public Broadcaster
    {
    public:
        /**
         * Send {@param buff} payload to other replicas.
         * @param buff buffer to broadcast
         * @return outcome::success on success or outcome::failure on error
         */
        outcome::result<void> Broadcast(const base::Buffer& buff, std::optional<std::string> topic_name) override;

        /**
         * Obtain the next {@return} payload received from the network.
         * @return buffer value or outcome::failure on error
         */
        outcome::result<std::tuple<base::Buffer, std::string>> Next() override;
        
        bool HasTopic(const std::string &topic) override;


        /** Queue of broadcast messages */
        std::queue<std::string> listOfBroadcasts_;
    };

} // namespace sgns::crdt

#endif // SUPERGENIUS_CRDT_CUSTOM_BROADCASTER_HPP