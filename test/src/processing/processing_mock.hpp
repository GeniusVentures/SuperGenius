
#ifndef PROCESSINGMOCK_HPP
#define PROCESSINGMOCK_HPP

#include "processing/processing_subtask_enqueuer_impl.hpp"
#include "processing/processing_validation_core.hpp"
#include "processing/processing_core.hpp"
#include <boost/functional/hash.hpp>
#include "testutil/wait_condition.hpp"
#include "processing/processing_engine.hpp"
#include "processing/processing_subtask_queue_accessor.hpp"
#include "processing/processing_subtask_result_storage.hpp"
#include "processing/processing_subtask_state_storage.hpp"

using namespace sgns::base;
using namespace sgns::processing;

namespace sgns::test
{
    class SubTaskStateStorageMock: public SubTaskStateStorage
    {
    public:
        void ChangeSubTaskState(const std::string& subTaskId, SGProcessing::SubTaskState::Type state) override {}
        std::optional<SGProcessing::SubTaskState> GetSubTaskState(const std::string& subTaskId) override
        {
            return std::nullopt;
        }
    };

    class SubTaskResultStorageMock : public SubTaskResultStorage
    {
    public:
        void AddSubTaskResult(const SGProcessing::SubTaskResult& subTaskResult) override {
            auto [_, success] = results.insert({subTaskResult.subtaskid(), subTaskResult});
            if (!success)
            {
                Color::PrintError("SubTaskResult ", subTaskResult.subtaskid(), " already added", " on node ", subTaskResult.node_address());
            }
            else
            {
                Color::PrintInfo("AddSubTaskResult ", subTaskResult.subtaskid(), " ", subTaskResult.node_address());
            }
        }

        void RemoveSubTaskResult(const std::string& subTaskId) override {
            results.erase(subTaskId);
        }

        std::vector<SGProcessing::SubTaskResult> GetSubTaskResults(const std::set<std::string>& subTaskIds) override {
            std::vector<SGProcessing::SubTaskResult> ret;
            for (const auto& id : subTaskIds) {
                if (results.count(id)) ret.push_back(results[id]);
            }
            return ret;
        }
    private:
        std::map<std::string, SGProcessing::SubTaskResult> results;
    };

    class ProcessingCoreImpl : public ProcessingCore
    {
    public:
        ProcessingCoreImpl(size_t processingMillisec = 50)
            : m_processingMillisec(processingMillisec)
        {
        }

        bool SetProcessingTypeFromJson(std::string jsondata) override
        {
            return true;
        }

        std::shared_ptr<std::pair<std::shared_ptr<std::vector<char>>, std::shared_ptr<std::vector<char>>>>  GetCidForProc(std::string json_data, std::string base_json) override
        {
            return nullptr;
        }

        void GetSubCidForProc(std::shared_ptr<boost::asio::io_context> ioc,std::string url, std::shared_ptr<std::vector<char>> results) override
        {
        }

        libp2p::outcome::result<SGProcessing::SubTaskResult> ProcessSubTask(
        const SGProcessing::SubTask& subTask, uint32_t initialHashCode) override
        {
            SGProcessing::SubTaskResult result;
            if (m_processingMillisec > 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(m_processingMillisec));
            }

            auto itResultHashes = m_chunkResultHashes.find(subTask.subtaskid());

            size_t subTaskResultHash = initialHashCode;
            for (int chunkIdx = 0; chunkIdx < subTask.chunkstoprocess_size(); ++chunkIdx)
            {
                size_t chunkHash = 0;
                if (itResultHashes != m_chunkResultHashes.end())
                {
                    chunkHash = itResultHashes->second[chunkIdx];
                }
                else
                {
                    const auto& chunk = subTask.chunkstoprocess(chunkIdx);
                    // Chunk result hash should be calculated
                    // Chunk data hash is calculated just as a stub
                    chunkHash = std::hash<std::string>{}(chunk.SerializeAsString());
                }

                std::string chunkHashString(reinterpret_cast<const char*>(&chunkHash), sizeof(chunkHash));
                result.add_chunk_hashes(chunkHashString);
                boost::hash_combine(subTaskResultHash, chunkHash);
            }

            std::string hashString(reinterpret_cast<const char*>(&subTaskResultHash), sizeof(subTaskResultHash));
            result.set_result_hash(hashString);

            auto [_, success] = m_processedSubTasks.insert(subTask.subtaskid());
            if (!success) {
                Color::PrintError("SubTask ", subTask.subtaskid(), " already processed");
            }
            m_initialHashes.push_back(initialHashCode);
            return result;
        };

        std::set<std::string> m_processedSubTasks;
        std::vector<uint32_t> m_initialHashes;

        std::map<std::string, std::vector<size_t>> m_chunkResultHashes;
    private:
        size_t m_processingMillisec;

    };

    class ProcessingTaskQueueImpl : public ProcessingTaskQueue
    {
    public:
        outcome::result<void> EnqueueTask( const SGProcessing::Task               &task,
                                           const std::list<SGProcessing::SubTask> &subTasks ) override
        {
            return outcome::success();
        }

        bool GetSubTasks(
            const std::string& taskId,
            std::list<SGProcessing::SubTask>& subTasks) override
        {
            return false;
        }

        outcome::result<std::pair<std::string, SGProcessing::Task>> GrabTask() override
        {
            return outcome::failure(boost::system::error_code{});
        }

        bool IsTaskCompleted( const std::string &taskId ) override
        {
            return true;
        }

        outcome::result<void> CompleteTask(const std::string& taskKey, const SGProcessing::TaskResult& task) override
        {
            return outcome::success();
        }
    };

    class ProcessingSubTaskQueueChannelImpl : public ProcessingSubTaskQueueChannel
    {
    public:
        typedef std::function<void(const std::string& nodeId)> QueueOwnershipRequestSink;
        typedef std::function<void(std::shared_ptr<SGProcessing::SubTaskQueue> queue)> QueuePublishingSink;

        void RequestQueueOwnership(const std::string& nodeId) override
        {
            if (queueOwnershipRequestSink)
            {
                queueOwnershipRequestSink(nodeId);
            }
        }

        void PublishQueue(std::shared_ptr<SGProcessing::SubTaskQueue> queue) override
        {
            if (queuePublishingSink)
            {
                queuePublishingSink(queue);
            }
        }

        QueueOwnershipRequestSink queueOwnershipRequestSink;
        QueuePublishingSink queuePublishingSink;

        size_t GetActiveNodesCount() const override
        {
            return 2;
        }

        std::vector<libp2p::peer::PeerId> GetActiveNodes() const override
        {
            const char* node1String = "Node_1";
            const char* node2String = "Node_2";
            gsl::span<const uint8_t> node1(
                reinterpret_cast<const uint8_t*>(node1String),
                std::strlen(node1String)
            );
            gsl::span<const uint8_t> node2(
                reinterpret_cast<const uint8_t*>(node2String),
                std::strlen(node2String)
            );

            static libp2p::peer::PeerId peer1 = libp2p::peer::PeerId::fromBytes(node1).value();
            static libp2p::peer::PeerId peer2 = libp2p::peer::PeerId::fromBytes(node2).value();
            return { peer1, peer2 };
        }
    };

        class SubTaskQueueAccessorMock : public SubTaskQueueAccessor
    {
    public:
        SubTaskQueueAccessorMock(boost::asio::io_context& context)
            : m_context(context)
            , m_timerToKeepContext(m_context)
        {
            m_timerToKeepContext.expires_from_now(boost::posix_time::seconds(5));
            m_timerToKeepContext.async_wait(
                std::bind(&SubTaskQueueAccessorMock::OnTimerEvent, this, std::placeholders::_1));
        }

        bool ConnectToSubTaskQueue(std::function<void()> onSubTaskQueueConnectedEventSink) override
        {
            m_onSubTaskQueueConnectedEventSink = onSubTaskQueueConnectedEventSink;
            return true;
        }

        bool AssignSubTasks(std::list<SGProcessing::SubTask>& subTasks) override
        {
            m_subTasks.swap(subTasks);
            if (m_onSubTaskQueueConnectedEventSink)
            {
                m_onSubTaskQueueConnectedEventSink();
            }
            return true;
        }

        void GrabSubTask(SubTaskGrabbedCallback onSubTaskGrabbedCallback) override
        {
            if (!m_subTasks.empty())
            {
                m_context.post([this, onSubTaskGrabbedCallback]() {
                    onSubTaskGrabbedCallback(m_subTasks.front());
                    m_subTasks.pop_front();
                });
            }
        }

        void CompleteSubTask(const std::string& subTaskId, const SGProcessing::SubTaskResult& subTaskResult) override
        {
            // Do nothing
        }
        bool CreateResultsChannel( const std::string &task_id ) override
        {
            return true;
        }

    private:
        std::list<SGProcessing::SubTask> m_subTasks;

        void OnTimerEvent(const boost::system::error_code& error)
        {
            m_timerToKeepContext.expires_from_now(boost::posix_time::seconds(5));
            m_timerToKeepContext.async_wait(
                std::bind(&SubTaskQueueAccessorMock::OnTimerEvent, this, std::placeholders::_1));
        }

        boost::asio::io_context& m_context;
        boost::asio::deadline_timer m_timerToKeepContext;

        std::function<void()> m_onSubTaskQueueConnectedEventSink;
    };


}

#endif //PROCESSINGMOCK_HPP
