/**
 * @file       crdt_mirror_broadcaster.cpp
 * @brief      
 * @date       2025-04-22
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "crdt_mirror_broadcaster.hpp"
#include "crdt/globaldb/proto/broadcast.pb.h"
#include <iostream>

namespace sgns::crdt
{
    void CRDTMirrorBroadcaster::SetMirrorCounterPart( const std::shared_ptr<CRDTMirrorBroadcaster> &dest )
    {
        counterpart_ = dest;
    }

    outcome::result<void> CRDTMirrorBroadcaster::Broadcast( const base::Buffer        &buff,
                                                              std::optional<std::string> topic_name )
    {
        if ( ( !buff.empty() ) && ( counterpart_ ) )
        {
            std::lock_guard<std::mutex> lock( counterpart_->mutex_ );
            broadcasting::BroadcastMessage bmsg;
            auto                                       bpi = new sgns::crdt::broadcasting::BroadcastMessage_PeerInfo;
            std::string data( buff.toString() );
            bmsg.set_data( data );
            bpi->set_id(data);
            bpi->add_addrs(data);
            bmsg.set_allocated_peer( bpi );
            const std::string           bCastData( bmsg.SerializeAsString() );
            
            counterpart_->listOfBroadcasts_.push( bCastData );
        }
        return outcome::success();
    }

    outcome::result<base::Buffer> CRDTMirrorBroadcaster::Next()
    {
        std::lock_guard<std::mutex> lock( mutex_ );
        if ( listOfBroadcasts_.empty() )
        {
            //Broadcaster::ErrorCode::ErrNoMoreBroadcast
            return outcome::failure( boost::system::error_code{} );
        }
        
        
        
        std::string strBuffer = listOfBroadcasts_.front();
        broadcasting::BroadcastMessage bmsg;

        if ( !bmsg.ParseFromString( strBuffer ) )
        {

            return outcome::failure( boost::system::error_code{} );

        }

        std::string data_str = bmsg.data();

        listOfBroadcasts_.pop();

        base::Buffer buffer;
        buffer.put( data_str );
        return buffer;
    }

    bool CRDTMirrorBroadcaster::HasTopic( const std::string &topic )
    {
        return true;
    }

} // namespace sgns::crdt
