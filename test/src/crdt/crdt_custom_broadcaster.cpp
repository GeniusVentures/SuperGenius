#include "crdt_custom_broadcaster.hpp"

namespace sgns::crdt
{
    outcome::result<void> CustomBroadcaster::Broadcast(const base::Buffer& buff, std::optional<std::string> topic_name)
    {
        if (!buff.empty())
        {
            const std::string bCastData(buff.toString());
            listOfBroadcasts_.push(bCastData);
        }
        return outcome::success();
    }
    outcome::result<std::tuple<base::Buffer, std::string>> CustomBroadcaster::Next()
    {
        if (listOfBroadcasts_.empty())
        {
            //Broadcaster::ErrorCode::ErrNoMoreBroadcast
            return outcome::failure(boost::system::error_code{});
        }

        std::string strBuffer = listOfBroadcasts_.front();
        listOfBroadcasts_.pop();

        base::Buffer buffer;
        buffer.put(strBuffer);
        return std::make_tuple(buffer, std::string(""));
    }
    bool CustomBroadcaster::hasTopic(const std::string &topic)
    {
        return true;
    }

} // namespace sgns::crdt