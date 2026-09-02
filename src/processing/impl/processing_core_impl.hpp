/**
 * @file       processing_core_impl.hpp
 * @brief      Header file of the Processing Core implementation that uses the ProcessingManager to execute subtasks.
 * @date       2024-03-28
 * @author     Justin Church (jchurch@gnus.ai)
 *             Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef PROCESSING_CORE_IMPL_HPP
#define PROCESSING_CORE_IMPL_HPP

#include <cmath>
#include <memory>
#include <iostream>
#include <utility>
#include <cstdint>

#include <functional>
#include <string>

#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <libp2p/multi/multibase_codec/multibase_codec_impl.hpp>
#include <libp2p/multi/content_identifier_codec.hpp>
#include <libp2p/injector/host_injector.hpp>
#include <libp2p/injector/kademlia_injector.hpp>
#include <libp2p/host/host.hpp>
#include <ipfs_pubsub/deny_list_connection_gater.hpp>

#include "processing/processing_core.hpp"
#include "processing/processing_task_queue.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "account/TokenID.hpp"

// Forward declaration
namespace sgns::sgprocessing
{
    class ProcessingManager;
}

namespace sgns::processing
{
    /**
     * @brief Default implementation of ProcessingCore backed by GlobalDB.
     */
    class ProcessingCoreImpl : public ProcessingCore
    {
    public:
        /**
         * @brief      Error codes for ProcessingCoreImpl operations.
         */
        enum class Error
        {
            MAX_NUMBER_SUBTASKS = 1,    ///< Number of subtasks exceeds the configured maximum
            GLOBALDB_READ_ERROR,        ///< Database read error
            NO_BUFFER_FROM_JOB_DATA,    ///< No buffer available from job data
            TASK_DESERIALIZATION_ERROR, ///< Error occurred while deserializing the task
            JOB_INCOMPATIBILITY_ERROR,  ///< Job is incompatible with the processing core
            INVALID_MODEL_ERROR,        ///< The model is invalid
            PNET_INITIALIZATION_ERROR   ///< Private-network (pnet) initialization of the processing host failed
        };

        /**
         * @brief      Host pieces produced by @c MakeGatedHostInjector.
         *
         *             The io_context is materialized eagerly (it is what the per-subtask
         *             processing run consumes); the gated libp2p Host itself is exposed
         *             lazily so the default processing path never pays for constructing
         *             an unused Host, while tests/tools can still prove the gated
         *             composition builds one.
         */
        struct GatedHostContext
        {
            /// IO context shared with the gated host composition (same injector).
            std::shared_ptr<boost::asio::io_context> io_context;
            /// Lazily creates the gated libp2p Host from the same injector; null if
            /// the composition was moved from.
            std::function<std::shared_ptr<libp2p::Host>()> make_host;
        };

        /**
         * @brief       Builds the per-subtask processing host composition with the same
         *              private-network enforcement the gossip host applies (D-11):
         *              Noise-only security (never Plaintext), the given connection gater,
         *              and - only when @c network_key is non-empty - the pnet PSK boundary
         *              (identical constructor path as the GossipPubSub pnet host).
         * @param[in]   network_key Private-network key text (swarm-key/base16/base64);
         *              empty string means public mode (no pnet binding).
         * @param[in]   gater Connection gater bound into the host injector.
         * @param[in]   kademlia_config Kademlia configuration (kept as before).
         * @return      The gated host context (eager io_context + lazy host factory).
         * @throws      libp2p::injector::PskValidationError (a std::exception) eagerly
         *              when the network key material is invalid - BEFORE any host can be
         *              assembled, so no half-configured host can exist (T-15-27).
         */
        static GatedHostContext MakeGatedHostInjector(
            const std::string                                          &network_key,
            const std::shared_ptr<sgns::ipfs_pubsub::DenyListConnectionGater> &gater,
            libp2p::protocol::kademlia::Config                           kademlia_config );

        /**
         * @brief       Factory method to create a new instance of ProcessingCoreImpl.
         * @param[in]   task_queue A shared pointer to the task queue used for retrieving tasks
         * @param[in]   maximalProcessingSubTaskCount The maximum number of subtasks that can be processed concurrently
         * @param[in]   tokenId The Token ID used on the results
         * @param[in]   network_key Private-network (pnet) key from the node configuration;
         *              empty (the default) keeps the public-mode construction, so existing
         *              callers and public nodes are unaffected.
         * @return      A shared pointer to the created ProcessingCoreImpl instance or nullptr if creation failed
         */
        static std::shared_ptr<ProcessingCoreImpl> New( std::shared_ptr<ProcessingTaskQueue> task_queue,
                                                        uint32_t                           maximalProcessingSubTaskCount,
                                                        TokenID                            tokenId,
                                                        std::string                        network_key = "" );

        ~ProcessingCoreImpl() = default;

        outcome::result<SGProcessing::SubTaskResult> ProcessSubTask( const SGProcessing::SubTask &subTask,
                                                                     uint32_t initialHashCode ) override;

        float GetProgress() const override;

    private:
        /**
         * @brief       Private constructor for ProcessingCoreImpl. Use the static New method to create instances.
         * @param[in]   task_queue A shared pointer to the task queue used for retrieving tasks
         * @param[in]   maximalProcessingSubTaskCount The maximum number of subtasks that can be processed concurrently
         * @param[in]   tokenId The Token ID used on the results
         * @param[in]   network_key Private-network (pnet) key; empty = public mode
         */
        explicit ProcessingCoreImpl( std::shared_ptr<ProcessingTaskQueue> task_queue,
                                     uint32_t                             maximalProcessingSubTaskCount,
                                     TokenID                              tokenId,
                                     std::string                          network_key = "" );

        /**
         * @brief       Increments the count of currently processing subtasks. Returns failure if the count exceeds the maximum allowed.
         * @return      Success if the count was incremented successfully, failure if the maximum number of processing subtasks has been exceeded
         */
        outcome::result<void> IncProcessingSubTaskCount();

        /**
         * @brief      Decrements the count of currently processing subtasks. Called whenever a processing finished.
         */
        void DecProcessingSubTaskCount();

        /// Shared pointer to the task queue for retrieving tasks and subtasks
        std::shared_ptr<ProcessingTaskQueue> task_queue_;
        /// The Token ID to be used on the results of the processed subtasks
        TokenID token_ID_;
        /// The maximum number of subtasks that can be processed concurrently.
        uint32_t max_processing_subtask_count_;
        /// Private-network (pnet) key from the node configuration; empty = public mode.
        /// Applied to every per-subtask host composition (D-11); never logged.
        std::string network_key_;
        /// Connection gater bound into every per-subtask host composition - the same
        /// deny-list gater type the gossip host uses.
        std::shared_ptr<sgns::ipfs_pubsub::DenyListConnectionGater> connection_gater_;

        /// Mutex to protect access to the processing subtask count
        std::mutex subtask_count_mutex_;
        /// The current count of subtasks being processed
        uint32_t processing_subtask_count_{ 0 };
        /// The last processing manager used for processing a subtask, kept here to allow progress retrieval during processing
        mutable std::shared_ptr<sgprocessing::ProcessingManager> processing_manager_;
    };
}

OUTCOME_HPP_DECLARE_ERROR_2( sgns::processing, ProcessingCoreImpl::Error );

#endif // PROCESSING_CORE_IMPL_HPP
