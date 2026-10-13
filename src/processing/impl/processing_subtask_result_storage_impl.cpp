#include "processing_subtask_result_storage_impl.hpp"
#include <utility>

#include "processing/impl/TaskKeys.hpp"

namespace sgns::processing
{
    SubTaskResultStorageImpl::SubTaskResultStorageImpl( std::shared_ptr<sgns::crdt::GlobalDB> db,
                                                        std::string                           processing_topic,
                                                        std::string                           private_network_id ) :
        m_db( std::move( db ) ),
        m_processing_topic( std::move( processing_topic ) ),
        m_private_network_id( std::move( private_network_id ) )
    {
    }

    SubTaskResultStorageImpl::~SubTaskResultStorageImpl() {}

    void SubTaskResultStorageImpl::AddSubTaskResult( const SGProcessing::SubTaskResult &result )
    {
        sgns::crdt::GlobalDB::Buffer data;
        data.put( result.SerializeAsString() );

        auto taskId = m_db->Put(
            sgns::crdt::HierarchicalKey( TaskKeys::SubTaskResultKey( m_private_network_id, result.subtaskid() ) ),
            data,
            { m_processing_topic } );
    }

    void SubTaskResultStorageImpl::RemoveSubTaskResult( const std::string &subTaskId )
    {
        m_db->Remove( sgns::crdt::HierarchicalKey( TaskKeys::SubTaskResultKey( m_private_network_id, subTaskId ) ),
                      { m_processing_topic } );
    }

    std::vector<SGProcessing::SubTaskResult> SubTaskResultStorageImpl::GetSubTaskResults(
        const std::set<std::string> &subTaskIds )
    {
        std::vector<SGProcessing::SubTaskResult> results;
        for ( const auto &subTaskId : subTaskIds )
        {
            auto data = m_db->Get(
                sgns::crdt::HierarchicalKey( TaskKeys::SubTaskResultKey( m_private_network_id, subTaskId ) ) );
            if ( data )
            {
                SGProcessing::SubTaskResult result;
                if ( result.ParseFromArray( data.value().data(), data.value().size() ) )
                {
                    results.push_back( std::move( result ) );
                }
            }
        }
        return results;
    }
}
