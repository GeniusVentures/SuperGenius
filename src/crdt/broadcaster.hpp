
#ifndef SUPERGENIUS_BROADCASTER_HPP
#define SUPERGENIUS_BROADCASTER_HPP

#include "base/buffer.hpp"

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
      Success = 0, /*> 0 should not represent an error */
      ErrNoMoreBroadcast = 1, /*> no more data to broadcast */
    };

    /**
    * Send {@param buff} payload to other replicas.
    * @return outcome::success on success or outcome::failure on error 
    */
    virtual outcome::result<void> Broadcast(const base::Buffer& buff, std::optional<std::string> topic_name = std::nullopt) = 0;

    /**
    * Obtain the next {@return} payload received from the network.
    * @return buffer value or outcome::failure on error 
    */
    virtual outcome::result<base::Buffer> Next() = 0;

    /**
     * @brief Checks whether the broadcaster is subscribed to the specified topic.
     *
     * @param topic The topic string to check.
     * @return true if the broadcaster is subscribed to the topic, false otherwise.
     */
    virtual bool hasTopic( const std::string &topic ) = 0;
  };
}  // namespace sgns::crdt 

#endif  // SUPERGENIUS_BROADCASTER_HPP
