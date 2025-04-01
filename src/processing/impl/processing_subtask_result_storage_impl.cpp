#include "processing_subtask_result_storage_impl.hpp"
#include <utility>
#include <boost/format.hpp>

namespace sgns::processing
{
    SubTaskResultStorageImpl::SubTaskResultStorageImpl( std::shared_ptr<sgns::crdt::GlobalDB> db,
                                                        const std::string                    &storageTopic ) :
        m_db( std::move( db ) ), m_storageTopic( storageTopic )
    {
    }

    void SubTaskResultStorageImpl::AddSubTaskResult( const SGProcessing::SubTaskResult &result )
    {
        sgns::crdt::GlobalDB::Buffer data;
        data.put( result.SerializeAsString() );

        auto taskId = m_db->Put(
            sgns::crdt::HierarchicalKey( ( boost::format( "results/%s" ) % result.subtaskid() ).str() ), data );
    }

    void SubTaskResultStorageImpl::RemoveSubTaskResult( const std::string &subTaskId )
    {
        m_db->Remove( sgns::crdt::HierarchicalKey( ( boost::format( "results/%s" ) % subTaskId ).str() ) );
    }

    std::vector<SGProcessing::SubTaskResult> SubTaskResultStorageImpl::GetSubTaskResults(
        const std::set<std::string> &subTaskIds )
    {
        std::vector<SGProcessing::SubTaskResult> results;
        for ( const auto &subTaskId : subTaskIds )
        {
            auto data = m_db->Get( sgns::crdt::HierarchicalKey( ( boost::format( "results/%s" ) % subTaskId ).str() ) );
            if (data)
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
