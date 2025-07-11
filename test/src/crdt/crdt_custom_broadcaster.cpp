#include "crdt_custom_broadcaster.hpp"
#include <iostream>

namespace sgns::crdt
{
    outcome::result<void> CustomBroadcaster::Broadcast(const base::Buffer& buff, std::string topic)
    {
        if (!buff.empty())
        {
            std::lock_guard<std::mutex> lock( mutex_ );
            const std::string bCastData(buff.toString());
            listOfBroadcasts_.push(bCastData);
        }
        return outcome::success();
    }
    
    outcome::result<base::Buffer> CustomBroadcaster::Next()
    {
        std::lock_guard<std::mutex> lock( mutex_ );
        if (listOfBroadcasts_.empty())
        {
            //Broadcaster::ErrorCode::ErrNoMoreBroadcast
            return outcome::failure(boost::system::error_code{});
        }

        std::string strBuffer = listOfBroadcasts_.front();
        listOfBroadcasts_.pop();

        base::Buffer buffer;
        buffer.put(strBuffer);
        return buffer;
    }
    bool CustomBroadcaster::HasTopic(const std::string &topic)
    {
        return true;
    }

} // namespace sgns::crdt