#include "processing_task_queue_impl.hpp"

#include <boost/format.hpp>

namespace sgns::processing
{

ProcessingTaskQueueImpl::ProcessingTaskQueueImpl(std::shared_ptr<sgns::crdt::GlobalDB> db)
    : m_db(db)
    , m_processingTimeout(std::chrono::seconds(10))
{
}

void ProcessingTaskQueueImpl::EnqueueTask(
    const std::string& taskId,
    const SGProcessing::Task& task,
    const std::list<SGProcessing::SubTask>& subTasks)
{
    auto taskKey = (boost::format("tasks/%s") % taskId).str();
    sgns::base::Buffer valueBuffer;
    valueBuffer.put(task.SerializeAsString());
    auto setKeyResult = m_db->Put(sgns::crdt::HierarchicalKey(taskKey), valueBuffer);
    if (setKeyResult.has_failure())
    {
        m_logger->debug("Unable to put key-value to CRDT datastore.");
    }

    // Check if data put
    auto getKeyResult = m_db->Get(sgns::crdt::HierarchicalKey(taskKey));
    if (getKeyResult.has_failure())
    {
        m_logger->debug("Unable to find key in CRDT datastore");
    }
    else
    {
        m_logger->debug("[{}] placed to GlobalDB ", taskKey);
        // getKeyResult.value().toString()
    }
    for (auto& subTask: subTasks)
    {
        auto subTaskKey = (boost::format("subtasks/%s/%s") % taskId % subTask.subtaskid()).str();
        valueBuffer.put(subTask.SerializeAsString());
        auto setKeyResult = m_db->Put(sgns::crdt::HierarchicalKey(subTaskKey), valueBuffer);
        if (setKeyResult.has_failure())
        {
            m_logger->debug("Unable to put key-value to CRDT datastore.");
        }

        // Check if data put
        auto getKeyResult = m_db->Get(sgns::crdt::HierarchicalKey(taskKey));
        if (getKeyResult.has_failure())
        {
            m_logger->debug("Unable to find key in CRDT datastore");
        }
        else
        {
            m_logger->debug("[{}] placed to GlobalDB ", taskKey);
            // getKeyResult.value().toString()
        }
    }
}

bool ProcessingTaskQueueImpl::GetSubTasks(
    const std::string& taskId,
    std::list<SGProcessing::SubTask>& subTasks)
{
    m_logger->debug("SUBTASKS_REQUESTED. TaskId: {}", taskId);
    auto key = (boost::format("subtasks/%s") % taskId).str();
    auto querySubTasks = m_db->QueryKeyValues(key);

    if (querySubTasks.has_failure())
    {
        m_logger->info("Unable list subtasks from CRDT datastore");
        return false;
    }

    if (querySubTasks.has_value())
    {
        m_logger->debug("SUBTASKS_FOUND {}", querySubTasks.value().size());

        for (auto element : querySubTasks.value())
        {
            SGProcessing::SubTask subTask;
            if (subTask.ParseFromArray(element.second.data(), element.second.size()))
            {
                subTasks.push_back(std::move(subTask));
            }
            else
            {
                m_logger->debug("Undable to parse a subtask");
            }
        }

        return true;
    }
    else
    {
        m_logger->debug("NO_SUBTASKS_FOUND. TaskId {}", taskId);
        return false;
    }
}

bool ProcessingTaskQueueImpl::GrabTask(std::string& grabbedTaskId, SGProcessing::Task& task)
{
    m_logger->info("GRAB_TASK");

    const char* taskKeyPrefix = "/tasks/";
    auto queryTasks = m_db->QueryKeyValues(taskKeyPrefix);
    if (queryTasks.has_failure())
    {
        m_logger->info("Unable list tasks from CRDT datastore");
        return false;
    }

    std::set<std::string> lockedTasks;
    if (queryTasks.has_value())
    {
        m_logger->info("TASK_QUEUE_SIZE: {}", queryTasks.value().size());
        bool isTaskGrabbed = false;
        for (auto element : queryTasks.value())
        {
            auto taskKey = m_db->StripElementKey(element.first.toString());
            if (taskKey.has_value())
            {
                bool isTaskLocked = IsTaskLocked(taskKey.value());
                m_logger->debug("TASK_QUEUE_ITEM: {}, LOCKED: {}", taskKey.value(), isTaskLocked);

                if (!isTaskLocked)
                {
                    if (task.ParseFromArray(element.second.data(), element.second.size()))
                    {
                        if (LockTask(taskKey.value()))
                        {
                            m_logger->debug("TASK_LOCKED {}", taskKey.value());
                            
                            grabbedTaskId = std::string(
                                taskKey.value().begin() + std::strlen(taskKeyPrefix), taskKey.value().end());
                            return true;
                        }
                    }
                }
                else
                {
                    m_logger->debug("TASK_PREVIOUSLY_LOCKED {}", taskKey.value());
                    lockedTasks.insert(taskKey.value());
                }
            }
            else
            {
                m_logger->debug("Undable to convert a key to string");
            }
        }

        // No task was grabbed so far
        for (auto lockedTask : lockedTasks)
        {
            if (MoveExpiredTaskLock(lockedTask, task))
            {
                grabbedTaskId = std::string(
                    lockedTask.begin() + std::strlen(taskKeyPrefix), lockedTask.end());
                return true;
            }
        }

    }
    return false;
}

bool ProcessingTaskQueueImpl::CompleteTask(const std::string& taskId, const SGProcessing::TaskResult& taskResult)
{
    sgns::base::Buffer data;
    data.put(taskResult.SerializeAsString());

    auto transaction = m_db->BeginTransaction();
    transaction->AddToDelta(sgns::crdt::HierarchicalKey("task_results/" + taskId), data);

    std::string taskKey = "tasks/" + taskId;
    transaction->RemoveFromDelta(sgns::crdt::HierarchicalKey("lock_" + taskKey));
    transaction->RemoveFromDelta(sgns::crdt::HierarchicalKey(taskKey));

    auto res = transaction->PublishDelta();
    m_logger->debug("TASK_COMPLETED: {}", taskKey);
    return !res.has_failure();
}

bool ProcessingTaskQueueImpl::IsTaskLocked(const std::string& taskKey)
{
    auto lockData = m_db->Get(sgns::crdt::HierarchicalKey("lock_" + taskKey));
    return (!lockData.has_failure() && lockData.has_value());
}

bool ProcessingTaskQueueImpl::LockTask(const std::string& taskKey)
{
    auto timestamp = std::chrono::system_clock::now();

    SGProcessing::TaskLock lock;
    lock.set_task_id(taskKey);
    lock.set_lock_timestamp(timestamp.time_since_epoch().count());

    sgns::base::Buffer lockData;
    lockData.put(lock.SerializeAsString());

    auto res = m_db->Put(sgns::crdt::HierarchicalKey("lock_" + taskKey), lockData);
    return !res.has_failure();
}

bool ProcessingTaskQueueImpl::MoveExpiredTaskLock(const std::string& taskKey, SGProcessing::Task& task)
{
    auto timestamp = std::chrono::system_clock::now();

    auto lockData = m_db->Get(sgns::crdt::HierarchicalKey("lock_" + taskKey));
    if (!lockData.has_failure() && lockData.has_value())
    {
        // Check task expiration
        SGProcessing::TaskLock lock;
        if (lock.ParseFromArray(lockData.value().data(), lockData.value().size()))
        {
            auto expirationTime =
                std::chrono::system_clock::time_point(
                    std::chrono::system_clock::duration(lock.lock_timestamp())) + m_processingTimeout;
            if (timestamp > expirationTime)
            {
                auto taskData = m_db->Get(taskKey);

                if (!taskData.has_failure())
                {
                    if (task.ParseFromArray(taskData.value().data(), taskData.value().size()))
                    {
                        if (LockTask(taskKey))
                        {
                            m_logger->debug("TASK_LOCK_MOVED {}", taskKey);
                            return true;
                        }
                    }
                }
                else
                {
                    m_logger->debug("Unable to find a task {}", taskKey);
                }
            }
        }
    }
    return false;
}
}
