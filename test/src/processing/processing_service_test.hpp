#ifndef PROCESSING_SERVICE_TEST_HPP
#define PROCESSING_SERVICE_TEST_HPP

#include <gtest/gtest.h>

#include <libp2p/crypto/ed25519_provider/ed25519_provider_impl.hpp>

#include "base/gossip_auth.hpp"
#include "processing_mock.hpp"
#include "processing/processing_engine.hpp"
#include "processing/processing_subtask_queue_accessor_impl.hpp"
#include "processing/processing_subtask_queue_channel_pubsub.hpp"
#include "processing/processing_subtask_queue_manager.hpp"
#include "processing/processing_service.hpp"

class ProcessingServiceTest : public ::testing::Test
{
public:
    void SetUp() override;
    void TearDown() override;
    virtual void Initialize(uint64_t numNodes, size_t processingTime);
    virtual void SetUp(std::string name, std::string loggerConfig);

    /**
     * public variables to share between test fixtures
     **/
    std::vector<std::shared_ptr<GossipPubSub>> m_pubsub_nodes;
    /// CR-G01 fixture repair: retained copies of the gossip host keypairs the
    /// nodes were constructed with -- gated surfaces seal with exactly these
    /// keys, so tests can seal sender-side payloads and wire signing keys.
    std::vector<std::shared_ptr<const libp2p::crypto::KeyPair>> m_pubsub_keypairs;
    std::vector<std::future<std::error_code>> m_pubsub_futures;

    std::vector<std::shared_ptr<sgns::test::ProcessingCoreImpl>> m_processing_cores;
    std::vector<std::shared_ptr<ProcessingSubTaskQueueChannelPubSub>> m_processing_queues_channel_pub_subs;

    std::vector<std::shared_ptr<ProcessingSubTaskQueueManager>> m_processing_queues_managers;

    std::vector<std::shared_ptr<ProcessingEngine>> m_processing_engines;

    std::vector<std::shared_ptr<SubTaskQueueAccessorImpl>> m_processing_queues_accessors;

    std::vector<std::unique_ptr<std::atomic<bool>>> m_IsTaskFinalized;

    // Track ProcessingServiceImpl instances to properly stop them in TearDown
    std::vector<std::shared_ptr<ProcessingServiceImpl>> m_processing_services;

    std::shared_ptr<soralog::Logger> m_Logger;

};

/// Generates a fresh Ed25519 gossip host keypair (distinct identity per node).
inline libp2p::crypto::KeyPair GenerateEd25519KeyPair()
{
    libp2p::crypto::ed25519::Ed25519ProviderImpl provider;
    auto keypair = provider.generate().value();
    libp2p::crypto::KeyPair result;
    result.publicKey  = { libp2p::crypto::Key::Type::Ed25519,
                         { keypair.public_key.begin(), keypair.public_key.end() } };
    result.privateKey = { libp2p::crypto::Key::Type::Ed25519,
                         { keypair.private_key.begin(), keypair.private_key.end() } };
    return result;
}

/// CR-G01 fixture helper: seals a serialized payload with a gossip host
/// keypair so it passes the gated receivers' OpenGossipPayload check (empty
/// vector on failure -- callers ASSERT before publishing).
inline std::vector<uint8_t> SealPayloadForKey( const libp2p::crypto::KeyPair &key,
                                               const std::string              &payload )
{
    auto from_bytes = sgns::base::DeriveGossipFromBytes( key );
    if ( from_bytes.has_error() )
    {
        return {};
    }
    auto sealed = sgns::base::SealGossipPayload(
        key,
        from_bytes.value(),
        gsl::span<const uint8_t>( reinterpret_cast<const uint8_t *>( payload.data() ), payload.size() ) );
    if ( sealed.has_error() )
    {
        return {};
    }
    return sealed.value();
}

#endif
