/**
 * @file multi_node_finality_fault_test.cpp
 * @brief Persistent four-peer production-route audit for Phase 12.
 */

#include <gtest/gtest.h>

#include "account/GeniusAccount.hpp"
#include "account/MintTransactionV2.hpp"
#include "account/TransactionManager.hpp"
#include "account/UTXOMerkle.hpp"
#include "base/hexutil.hpp"
#include "blockchain/Blockchain.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "crdt/crdt_options.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "crypto/hasher.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/TestMintInputValidator.hpp"
#include "testutil/wait_condition.hpp"

#include <array>
#include <boost/filesystem.hpp>
#include <chrono>
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <libp2p/basic/scheduler/scheduler_impl.hpp>
#include <memory>
#include <thread>

namespace sgns
{
    class MultiNodeFinalityFaultTestAccess
    {
    public:
        static std::shared_ptr<ConsensusManager> Manager( const std::shared_ptr<Blockchain> &blockchain )
        {
            return blockchain ? blockchain->consensus_manager_ : nullptr;
        }

        static uint64_t VotePublications( const std::shared_ptr<ConsensusManager> &manager )
        {
            if ( !manager ) return 0;
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->fault_test_counters_.vote_publications;
        }

        static uint64_t CertificateNotificationsReceived( const std::shared_ptr<ConsensusManager> &manager )
        {
            if ( !manager ) return 0;
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->fault_test_counters_.certificate_notifications_received;
        }

        static uint64_t CertificateNotificationsPublished( const std::shared_ptr<ConsensusManager> &manager )
        {
            if ( !manager ) return 0;
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->fault_test_counters_.certificate_notification_publications;
        }

        static uint64_t CertificateWriteAttempts( const std::shared_ptr<ConsensusManager> &manager )
        {
            if ( !manager ) return 0;
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->fault_test_counters_.certificate_write_attempts;
        }

        static uint64_t CertificateWriteSuccesses( const std::shared_ptr<ConsensusManager> &manager )
        {
            if ( !manager ) return 0;
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->fault_test_counters_.certificate_write_successes;
        }

        static uint64_t AcceptedCertificateReadbacks( const std::shared_ptr<ConsensusManager> &manager )
        {
            if ( !manager ) return 0;
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->fault_test_counters_.accepted_certificate_readbacks;
        }

        static std::optional<std::string> DurableActiveVoteProposalId( const std::shared_ptr<ConsensusManager> &manager,
                                                                       const std::string &slot )
        {
            if ( !manager || !manager->db_ ) return std::nullopt;
            crdt::GlobalDB::Buffer key;
            key.put( manager->ActiveVoteStorageKey( slot ) );
            const auto stored = manager->db_->GetDataStore()->get( key );
            if ( stored.has_error() ) return std::nullopt;
            const auto decoded = manager->DecodeActiveVoteRecord( slot, stored.value().toString() );
            return decoded.has_value() ? std::optional<std::string>( decoded.value().proposal.proposal_id() ) : std::nullopt;
        }

        static uint64_t MintEffects( const TransactionManager &transactions )
        {
            std::lock_guard lock( transactions.fault_test_mutex_ );
            return transactions.mint_effects_for_test_;
        }

        static std::string SlotKey( const ConsensusManager::Proposal &proposal )
        {
            return ConsensusManager::GetSlotKey( proposal );
        }

        static std::optional<std::string> NonceTransactionHash( const ConsensusManager::Proposal &proposal )
        {
            const auto nonce_subject = ConsensusManager::DecodeNonceSubject( proposal.subject() );
            return nonce_subject.has_value() ? std::optional<std::string>( nonce_subject.value().tx_hash() ) : std::nullopt;
        }

        static void ArmActiveVoteBarrier( const std::shared_ptr<ConsensusManager> &manager )
        {
            std::lock_guard lock( manager->fault_test_mutex_ );
            manager->active_vote_persisted_barrier_ = { true, false, false };
        }

        static void ArmAcceptedCertificateBarrier( const std::shared_ptr<ConsensusManager> &manager )
        {
            std::lock_guard lock( manager->fault_test_mutex_ );
            manager->accepted_certificate_barrier_ = { true, false, false };
        }

        static bool ActiveVoteBarrierEntered( const std::shared_ptr<ConsensusManager> &manager )
        {
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->active_vote_persisted_barrier_.entered;
        }

        static bool AcceptedCertificateBarrierEntered( const std::shared_ptr<ConsensusManager> &manager )
        {
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->accepted_certificate_barrier_.entered;
        }

        static void ReleaseConsensusBarriers( const std::shared_ptr<ConsensusManager> &manager )
        {
            if ( !manager ) return;
            std::lock_guard lock( manager->fault_test_mutex_ );
            manager->active_vote_persisted_barrier_.released = true;
            manager->certificate_persisted_barrier_.released = true;
            manager->accepted_certificate_barrier_.released = true;
            manager->fault_test_cv_.notify_all();
        }

        static void ArmMintEffectsBarrier( const std::shared_ptr<TransactionManager> &transactions )
        {
            std::lock_guard lock( transactions->fault_test_mutex_ );
            transactions->mint_effects_barrier_ = { true, false, false };
        }

        static bool MintEffectsBarrierEntered( const std::shared_ptr<TransactionManager> &transactions )
        {
            std::lock_guard lock( transactions->fault_test_mutex_ );
            return transactions->mint_effects_barrier_.entered;
        }

        static void ReleaseMintEffectsBarrier( const std::shared_ptr<TransactionManager> &transactions )
        {
            if ( !transactions ) return;
            std::lock_guard lock( transactions->fault_test_mutex_ );
            transactions->mint_effects_barrier_.released = true;
            transactions->fault_test_cv_.notify_all();
        }
    };
} // namespace sgns

namespace
{
    const auto kToken = sgns::TokenID::FromBytes( { 0x00 } );

    class FinalityFaultNetwork : public ::test::CRDTFixture
    {
    protected:
        struct Peer
        {
            std::string                                      name;
            uint16_t                                         port = 0;
            std::string                                      root;
            std::shared_ptr<boost::asio::io_context>         io;
            std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub;
            std::shared_ptr<sgns::crdt::GlobalDB>            db;
            std::shared_ptr<sgns::GeniusAccount>             account;
            std::shared_ptr<sgns::Blockchain>                blockchain;
            std::shared_ptr<sgns::TransactionManager>        transactions;
            std::shared_ptr<sgns::ConsensusManager>          consensus;
            std::thread                                      io_thread;

            Peer() = default;
            Peer( const Peer & ) = delete;
            Peer &operator=( const Peer & ) = delete;
            Peer( Peer && ) noexcept = default;
            Peer &operator=( Peer && ) noexcept = default;
        };

        struct Network
        {
            Peer first;
            Peer second;
            Peer third;
            Peer passive;
        };

        FinalityFaultNetwork() : CRDTFixture( "multi_node_finality_fault" )
        {
        }

        Peer StartPeer( const std::string &name, uint16_t port )
        {
            Peer peer;
            peer.name = name;
            peer.port = port;
            peer.root = ( base_path / name ).string();
            boost::filesystem::create_directories( peer.root );
            peer.io = std::make_shared<boost::asio::io_context>();
            auto keypair = sgns::crdt::KeyPairFileStorage( peer.root + "/keypair" ).GetKeyPair();
            EXPECT_TRUE( keypair.has_value() );
            if ( keypair.has_error() ) return peer;
            peer.pubsub = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>( keypair.value() );
            EXPECT_TRUE( peer.pubsub );
            if ( !peer.pubsub ) return peer;
            EXPECT_FALSE( peer.pubsub->Start( port, { peer.pubsub->GetLocalAddress() } ).get() );
            auto scheduler = std::make_shared<libp2p::basic::SchedulerImpl>(
                std::make_shared<libp2p::basic::AsioSchedulerBackend>( peer.io ),
                libp2p::basic::Scheduler::Config{ std::chrono::milliseconds( 100 ) } );
            auto graphsync = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( peer.pubsub->GetHost(), scheduler );
            auto generator = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();
            auto db = sgns::crdt::GlobalDB::New( peer.io, peer.root + "/rocksdb", peer.pubsub,
                                                 sgns::crdt::CrdtOptions::DefaultOptions(), graphsync, scheduler, generator );
            EXPECT_TRUE( db.has_value() );
            if ( db.has_error() ) return peer;
            peer.db = std::move( db.value() );
            peer.db->Start();
            peer.io_thread = std::thread( [io = peer.io] { io->run(); } );
            peer.account = sgns::GeniusAccount::New( kToken, peer.root + "/account" );
            EXPECT_TRUE( peer.account );
            if ( !peer.account ) return peer;
            EXPECT_TRUE( peer.account->GetUTXOManager().LoadUTXOs( peer.db->GetDataStore() ).has_value() );
            peer.blockchain = sgns::Blockchain::New( peer.db, peer.account, peer.pubsub, []( outcome::result<void> ) {} );
            EXPECT_TRUE( peer.blockchain );
            if ( !peer.blockchain ) return peer;
            peer.transactions = sgns::TransactionManager::New( peer.db, peer.io, peer.account, peer.blockchain, false );
            EXPECT_TRUE( peer.transactions );
            peer.consensus = sgns::MultiNodeFinalityFaultTestAccess::Manager( peer.blockchain );
            EXPECT_TRUE( peer.consensus );
            return peer;
        }

        void StopPeer( Peer &peer )
        {
            if ( peer.transactions ) peer.transactions->Stop();
            peer.transactions.reset();
            if ( peer.blockchain ) (void) peer.blockchain->Stop();
            peer.consensus.reset();
            peer.blockchain.reset();
            if ( peer.io ) peer.io->stop();
            if ( peer.io_thread.joinable() ) peer.io_thread.join();
            if ( peer.pubsub ) peer.pubsub->Stop();
            peer.db.reset();
            peer.pubsub.reset();
            peer.account.reset();
            peer.io.reset();
        }

        void RestartPeer( Peer &peer )
        {
            const auto name = peer.name;
            const auto port = peer.port;
            StopPeer( peer );
            peer = StartPeer( name, port );
        }

        Network StartNetwork( const std::string &prefix, uint16_t first_port )
        {
            return { StartPeer( prefix + "-validator-one", first_port ),
                     StartPeer( prefix + "-validator-two", static_cast<uint16_t>( first_port + 1 ) ),
                     StartPeer( prefix + "-validator-three", static_cast<uint16_t>( first_port + 2 ) ),
                     StartPeer( prefix + "-passive", static_cast<uint16_t>( first_port + 3 ) ) };
        }

        static std::array<Peer *, 4> Peers( Network &network )
        {
            return { &network.first, &network.second, &network.third, &network.passive };
        }

        void StoreRegistry( Network &network )
        {
            const auto update = RegistryUpdate( { &network.first, &network.second, &network.third } );
            for ( auto *peer : Peers( network ) )
                ASSERT_TRUE( peer->blockchain->GetValidatorRegistry()->StoreRegistryUpdate( update ).has_value() );
            ASSERT_WAIT_FOR_CONDITION( [&] {
                return network.first.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
                       network.second.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
                       network.third.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
                       network.passive.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value();
            }, std::chrono::seconds( 5 ), "every peer durably stored the validator registry", nullptr );
        }

        void RestartAndReconnect( Network &network )
        {
            RestartPeer( network.first );
            RestartPeer( network.second );
            RestartPeer( network.third );
            RestartPeer( network.passive );
            ASSERT_TRUE( network.first.consensus && network.second.consensus && network.third.consensus && network.passive.consensus );
            ConnectPeers( Peers( network ) );
        }

        void StopNetwork( Network &network )
        {
            StopPeer( network.first );
            StopPeer( network.second );
            StopPeer( network.third );
            StopPeer( network.passive );
        }

        static void ConnectPeers( const std::array<Peer *, 4> &peers )
        {
            for ( auto *source : peers )
                for ( auto *target : peers )
                    if ( source != target ) source->pubsub->AddPeers( { target->pubsub->GetInterfaceAddress() } );
        }

        static sgns::ValidatorRegistry::RegistryUpdate RegistryUpdate( const std::array<Peer *, 3> &validators )
        {
            sgns::ValidatorRegistry::RegistryUpdate update;
            update.mutable_registry()->set_epoch( 1 );
            for ( auto *peer : validators )
            {
                auto *validator = update.mutable_registry()->add_validators();
                validator->set_validator_id( peer->account->GetAddress() );
                validator->set_weight( 1 );
                validator->set_role( sgns::ValidatorRegistry::Role::REGULAR );
                validator->set_status( sgns::ValidatorRegistry::Status::ACTIVE );
            }
            sgns::validator::RegistrySigningPayload payload;
            *payload.mutable_registry() = update.registry();
            std::string bytes;
            EXPECT_TRUE( payload.SerializeToString( &bytes ) );
            auto signature = validators.front()->account->Sign( std::vector<uint8_t>( bytes.begin(), bytes.end() ) );
            auto *entry = update.add_signatures();
            entry->set_validator_id( validators.front()->account->GetAddress() );
            entry->set_signature( signature.data(), signature.size() );
            return update;
        }

        static std::shared_ptr<sgns::MintTransactionV2> MintFor( const Peer &peer, uint64_t timestamp_offset = 0 )
        {
            SGTransaction::DAGStruct dag;
            dag.set_type( "mint-v2" );
            dag.set_source_addr( peer.account->GetAddress() );
            dag.set_nonce( 0 );
            dag.set_timestamp( std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::system_clock::now().time_since_epoch() ).count() + timestamp_offset );
            const auto burn_hash = sgns::base::Hash256::fromReadableString( std::string( 64, 'c' ) );
            EXPECT_TRUE( burn_hash.has_value() );
            dag.set_uncle_hash( burn_hash.value().toReadableString() );
            auto mint = std::make_shared<sgns::MintTransactionV2>( sgns::MintTransactionV2::New(
                42, "test", kToken, std::move( dag ), { { burn_hash.value(), 0, {} } }, peer.account->GetAddress() ) );
            mint->MakeSignature( *peer.account );
            return mint;
        }

        static sgns::UTXOTransitionCommitment CommitmentFor( const sgns::MintTransactionV2 &mint )
        {
            sgns::UTXOTransitionCommitment commitment;
            const auto params = mint.GetUTXOParameters();
            std::vector<std::vector<uint8_t>> consumed_payloads;
            for ( const auto &input : params.first )
            {
                auto *consumed = commitment.add_consumed_outpoints();
                consumed->set_tx_id_hash( input.txid_hash_.data(), input.txid_hash_.size() );
                consumed->set_output_index( input.output_idx_ );

                std::vector<uint8_t> payload( input.txid_hash_.begin(), input.txid_hash_.end() );
                sgns::utxo_merkle::AppendUInt32BE( payload, input.output_idx_ );
                consumed_payloads.push_back( std::move( payload ) );
            }

            const auto mint_hash = sgns::base::Hash256::fromReadableString( mint.GetHash() );
            EXPECT_TRUE( mint_hash.has_value() );
            std::vector<std::vector<uint8_t>> produced_payloads;
            for ( uint32_t index = 0; index < params.second.size(); ++index )
            {
                const auto &output = params.second[index];
                sgns::GeniusUTXO produced( mint_hash.value(), index, output.encrypted_amount, output.token_id, output.dest_address );
                auto *committed = commitment.add_produced_outputs();
                committed->set_tx_id_hash( mint_hash.value().data(), mint_hash.value().size() );
                committed->set_output_index( index );
                committed->set_owner_address( output.dest_address );
                const auto token_bytes = output.token_id.bytes();
                committed->set_token_id( token_bytes.data(), token_bytes.size() );
                committed->set_amount( output.encrypted_amount );
                produced_payloads.push_back( sgns::utxo_merkle::SerializeUTXOLeafPayload( produced ) );
            }

            const auto consumed_root = sgns::utxo_merkle::ComputeMerkleRootFromPayloads( std::move( consumed_payloads ) );
            const auto produced_root = sgns::utxo_merkle::ComputeMerkleRootFromPayloads( std::move( produced_payloads ) );
            commitment.set_consumed_outpoints_root( consumed_root.data(), consumed_root.size() );
            commitment.set_produced_outputs_root( produced_root.data(), produced_root.size() );
            return commitment;
        }

        static bool HasBridgeMarker( const Peer &peer, const sgns::MintTransactionV2 &mint )
        {
            sgns::crdt::GlobalDB::Buffer marker_key;
            marker_key.put( "/bridge/executed/" + mint.GetChainId() + ":" + mint.dag_st.uncle_hash() );
            return peer.db && peer.db->GetDataStore()->get( marker_key ).has_value();
        }

        static bool HasOnlyWinnerOutput( const Peer &peer, const sgns::MintTransactionV2 &winner,
                                         const sgns::MintTransactionV2 &loser )
        {
            const auto outputs = peer.account->GetUTXOManager().GetUTXOs( winner.dag_st.source_addr() );
            return outputs.size() == 1 && outputs.front().GetTxID().toReadableString() == winner.GetHash() &&
                   outputs.front().GetTxID().toReadableString() != loser.GetHash();
        }

        static void AssertSingleDurableMint( const Peer &peer, const std::string &slot,
                                             const sgns::MintTransactionV2 &winner,
                                             const sgns::MintTransactionV2 &loser,
                                             uint64_t expected_mint_effects )
        {
            ASSERT_TRUE( peer.consensus->CheckCertificateForSlot( slot ) );
            const auto certificate = peer.consensus->GetCertificateBySlot( slot );
            ASSERT_TRUE( certificate.has_value() );
            EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::NonceTransactionHash( certificate.value().proposal() ),
                       winner.GetHash() );
            EXPECT_TRUE( HasOnlyWinnerOutput( peer, winner, loser ) );
            EXPECT_TRUE( HasBridgeMarker( peer, winner ) );
            EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *peer.transactions ), expected_mint_effects );
        }
    };
} // namespace

TEST_F( FinalityFaultNetwork, ProductionRouteAuditUsesOnlyPubSubCrdtPersistenceAndMintIngress )
{
    sgns::GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier )
                                                    { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );
    auto first = StartPeer( "validator-one", 54301 );
    auto second = StartPeer( "validator-two", 54302 );
    auto third = StartPeer( "validator-three", 54303 );
    auto passive = StartPeer( "passive-recipient", 54304 );
    ASSERT_TRUE( first.consensus && second.consensus && third.consensus && passive.consensus );

    const std::array<Peer *, 4> peers{ &first, &second, &third, &passive };
    ConnectPeers( peers );
    ASSERT_WAIT_FOR_CONDITION( [&] { return first.db && second.db && third.db && passive.db; },
                               std::chrono::seconds( 3 ), "four real peers started and connected", nullptr );

    const auto update = RegistryUpdate( { &first, &second, &third } );
    for ( auto *peer : { &first, &second, &third, &passive })
        ASSERT_TRUE( peer->blockchain->GetValidatorRegistry()->StoreRegistryUpdate( update ).has_value() );
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return first.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
               second.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
               third.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
               passive.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value();
    }, std::chrono::seconds( 5 ), "validator registry durably available on every peer", nullptr );

    auto mint = MintFor( first );
    ASSERT_TRUE( mint );
    auto subject = sgns::ConsensusManager::CreateNonceSubject( first.account->GetAddress(), mint->GetNonce(), mint->GetHash(),
                                                                mint->SerializeToEmbeddedTransaction(), CommitmentFor( *mint ), sgns::UTXOWitness{} );
    ASSERT_TRUE( subject.has_value() );
    auto proposal = first.consensus->CreateProposal( subject.value(), first.account->GetAddress(),
                                                     first.blockchain->GetValidatorRegistry()->GetRegistryCid(),
                                                     first.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
    ASSERT_TRUE( proposal.has_value() );
    const auto canonical_slot = sgns::MultiNodeFinalityFaultTestAccess::SlotKey( proposal.value() );
    ASSERT_FALSE( canonical_slot.empty() );
    ASSERT_TRUE( first.consensus->SubmitProposal( proposal.value() ).has_value() );
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return sgns::MultiNodeFinalityFaultTestAccess::VotePublications( first.consensus ) > 0 &&
               sgns::MultiNodeFinalityFaultTestAccess::VotePublications( second.consensus ) > 0 &&
               sgns::MultiNodeFinalityFaultTestAccess::VotePublications( third.consensus ) > 0;
    }, std::chrono::seconds( 15 ), "public proposal reached durable votes on all validators", nullptr );
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return first.consensus->CheckCertificateForSlot( canonical_slot ) &&
               second.consensus->CheckCertificateForSlot( canonical_slot ) &&
               third.consensus->CheckCertificateForSlot( canonical_slot ) &&
               passive.consensus->CheckCertificateForSlot( canonical_slot );
    }, std::chrono::seconds( 20 ), "one canonical certificate reached every peer through CRDT", nullptr );
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return sgns::MultiNodeFinalityFaultTestAccess::CertificateNotificationsPublished( first.consensus ) +
                   sgns::MultiNodeFinalityFaultTestAccess::CertificateNotificationsPublished( second.consensus ) +
                   sgns::MultiNodeFinalityFaultTestAccess::CertificateNotificationsPublished( third.consensus ) >
               0 &&
               sgns::MultiNodeFinalityFaultTestAccess::CertificateNotificationsReceived( passive.consensus ) > 0 &&
               sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *first.transactions ) == 1 &&
               sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *second.transactions ) == 1 &&
               sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *third.transactions ) == 1 &&
               sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *passive.transactions ) == 1;
    }, std::chrono::seconds( 20 ), "certificate notification reached the registered Mint consumers", nullptr );

    const auto losing_mint = MintFor( first, 1 );
    ASSERT_TRUE( losing_mint );
    ASSERT_EQ( mint->GetSlotID(), losing_mint->GetSlotID() );
    ASSERT_NE( mint->GetHash(), losing_mint->GetHash() );
    auto losing_subject = sgns::ConsensusManager::CreateNonceSubject(
        first.account->GetAddress(), losing_mint->GetNonce(), losing_mint->GetHash(),
        losing_mint->SerializeToEmbeddedTransaction(), CommitmentFor( *losing_mint ), sgns::UTXOWitness{} );
    ASSERT_TRUE( losing_subject.has_value() );
    auto losing_proposal = second.consensus->CreateProposal( losing_subject.value(), second.account->GetAddress(),
                                                              second.blockchain->GetValidatorRegistry()->GetRegistryCid(),
                                                              second.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
    ASSERT_TRUE( losing_proposal.has_value() );
    ASSERT_TRUE( second.consensus->SubmitProposal( losing_proposal.value() ).has_value() );

    for ( auto *peer : peers )
    {
        const auto certificate = peer->consensus->GetCertificateBySlot( canonical_slot );
        ASSERT_TRUE( certificate.has_value() );
        EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::NonceTransactionHash( certificate.value().proposal() ),
                   mint->GetHash() );
        EXPECT_TRUE( HasOnlyWinnerOutput( *peer, *mint, *losing_mint ) );
        EXPECT_TRUE( HasBridgeMarker( *peer, *mint ) );
    }

    RestartPeer( first );
    RestartPeer( second );
    RestartPeer( third );
    RestartPeer( passive );
    ASSERT_TRUE( first.consensus && second.consensus && third.consensus && passive.consensus );
    ConnectPeers( { &first, &second, &third, &passive } );
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return first.consensus->CheckCertificateForSlot( canonical_slot ) &&
               second.consensus->CheckCertificateForSlot( canonical_slot ) &&
               third.consensus->CheckCertificateForSlot( canonical_slot ) &&
               passive.consensus->CheckCertificateForSlot( canonical_slot ) && HasOnlyWinnerOutput( first, *mint, *losing_mint ) &&
               HasOnlyWinnerOutput( second, *mint, *losing_mint ) && HasOnlyWinnerOutput( third, *mint, *losing_mint ) &&
               HasOnlyWinnerOutput( passive, *mint, *losing_mint ) && HasBridgeMarker( first, *mint ) &&
               HasBridgeMarker( second, *mint ) && HasBridgeMarker( third, *mint ) && HasBridgeMarker( passive, *mint );
    }, std::chrono::seconds( 20 ), "certificate and exact winner Mint survived every peer recreation", nullptr );
    StopPeer( first ); StopPeer( second ); StopPeer( third ); StopPeer( passive );
}

TEST_F( FinalityFaultNetwork, SameBurnContentionUsesOneCanonicalSlotAndExactMint )
{
    sgns::GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier )
                                                    { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );
    auto first = StartPeer( "contention-validator-one", 54401 );
    auto second = StartPeer( "contention-validator-two", 54402 );
    auto third = StartPeer( "contention-validator-three", 54403 );
    auto passive = StartPeer( "contention-passive-recipient", 54404 );
    ASSERT_TRUE( first.consensus && second.consensus && third.consensus && passive.consensus );

    const std::array<Peer *, 4> peers{ &first, &second, &third, &passive };
    const auto update = RegistryUpdate( { &first, &second, &third } );
    for ( auto *peer : peers )
        ASSERT_TRUE( peer->blockchain->GetValidatorRegistry()->StoreRegistryUpdate( update ).has_value() );
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return first.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
               second.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
               third.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
               passive.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value();
    }, std::chrono::seconds( 5 ), "disconnected peers durably stored the validator registry", nullptr );

    auto first_mint = MintFor( first );
    auto second_mint = MintFor( first, 1 );
    ASSERT_TRUE( first_mint && second_mint );
    ASSERT_EQ( first_mint->GetSlotID(), second_mint->GetSlotID() );
    ASSERT_NE( first_mint->GetHash(), second_mint->GetHash() );

    auto first_subject = sgns::ConsensusManager::CreateNonceSubject(
        first.account->GetAddress(), first_mint->GetNonce(), first_mint->GetHash(),
        first_mint->SerializeToEmbeddedTransaction(), CommitmentFor( *first_mint ), sgns::UTXOWitness{} );
    auto second_subject = sgns::ConsensusManager::CreateNonceSubject(
        first.account->GetAddress(), second_mint->GetNonce(), second_mint->GetHash(),
        second_mint->SerializeToEmbeddedTransaction(), CommitmentFor( *second_mint ), sgns::UTXOWitness{} );
    ASSERT_TRUE( first_subject.has_value() && second_subject.has_value() );
    auto first_proposal = first.consensus->CreateProposal( first_subject.value(), first.account->GetAddress(),
                                                            first.blockchain->GetValidatorRegistry()->GetRegistryCid(),
                                                            first.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
    auto second_proposal = second.consensus->CreateProposal( second_subject.value(), second.account->GetAddress(),
                                                              second.blockchain->GetValidatorRegistry()->GetRegistryCid(),
                                                              second.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
    ASSERT_TRUE( first_proposal.has_value() && second_proposal.has_value() );
    const auto canonical_slot = sgns::MultiNodeFinalityFaultTestAccess::SlotKey( first_proposal.value() );
    ASSERT_EQ( canonical_slot, sgns::MultiNodeFinalityFaultTestAccess::SlotKey( second_proposal.value() ) );
    ASSERT_TRUE( first.consensus->SubmitProposal( first_proposal.value() ).has_value() );
    ASSERT_TRUE( second.consensus->SubmitProposal( second_proposal.value() ).has_value() );

    // Named real-peer barrier: the three validators connect only after both
    // public proposal submissions have completed on their isolated peers.
    for ( auto *source : { &first, &second, &third } )
        for ( auto *target : { &first, &second, &third } )
            if ( source != target ) source->pubsub->AddPeers( { target->pubsub->GetInterfaceAddress() } );
    // GossipPubSub does not replay publications made before a peer link exists.
    // Re-advertise the same public proposals after the real link barrier; this
    // exercises ordinary production ingress without synthesizing delivery.
    ASSERT_TRUE( first.consensus->SubmitProposal( first_proposal.value() ).has_value() );
    ASSERT_TRUE( second.consensus->SubmitProposal( second_proposal.value() ).has_value() );
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return first.consensus->CheckCertificateForSlot( canonical_slot ) &&
               second.consensus->CheckCertificateForSlot( canonical_slot ) &&
               third.consensus->CheckCertificateForSlot( canonical_slot );
    }, std::chrono::seconds( 20 ), "connected validators converged on one durable canonical certificate", nullptr );

    const auto certificate = first.consensus->GetCertificateBySlot( canonical_slot );
    ASSERT_TRUE( certificate.has_value() );
    const auto winner_hash = sgns::MultiNodeFinalityFaultTestAccess::NonceTransactionHash( certificate.value().proposal() );
    ASSERT_TRUE( winner_hash.has_value() );
    const auto winner = winner_hash.value() == first_mint->GetHash() ? first_mint : second_mint;
    const auto loser = winner_hash.value() == first_mint->GetHash() ? second_mint : first_mint;
    ASSERT_NE( winner->GetHash(), loser->GetHash() );

    for ( auto *validator : { &first, &second, &third } )
    {
        const auto durable = validator->consensus->GetCertificateBySlot( canonical_slot );
        ASSERT_TRUE( durable.has_value() );
        EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::NonceTransactionHash( durable.value().proposal() ), winner_hash );
    }

    ConnectPeers( peers );
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return passive.consensus->CheckCertificateForSlot( canonical_slot ) &&
               sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *first.transactions ) == 1 &&
               sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *second.transactions ) == 1 &&
               sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *third.transactions ) == 1 &&
               sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *passive.transactions ) == 1;
    }, std::chrono::seconds( 20 ), "passive peer received and recovered the exact canonical winner", nullptr );
    for ( auto *peer : peers )
    {
        EXPECT_TRUE( HasOnlyWinnerOutput( *peer, *winner, *loser ) );
        EXPECT_TRUE( HasBridgeMarker( *peer, *winner ) );
    }

    RestartPeer( first ); RestartPeer( second ); RestartPeer( third ); RestartPeer( passive );
    ASSERT_TRUE( first.consensus && second.consensus && third.consensus && passive.consensus );
    ConnectPeers( { &first, &second, &third, &passive } );
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return first.consensus->CheckCertificateForSlot( canonical_slot ) &&
               second.consensus->CheckCertificateForSlot( canonical_slot ) &&
               third.consensus->CheckCertificateForSlot( canonical_slot ) &&
               passive.consensus->CheckCertificateForSlot( canonical_slot ) &&
               HasOnlyWinnerOutput( first, *winner, *loser ) && HasOnlyWinnerOutput( second, *winner, *loser ) &&
               HasOnlyWinnerOutput( third, *winner, *loser ) && HasOnlyWinnerOutput( passive, *winner, *loser ) &&
               HasBridgeMarker( first, *winner ) && HasBridgeMarker( second, *winner ) &&
               HasBridgeMarker( third, *winner ) && HasBridgeMarker( passive, *winner );
    }, std::chrono::seconds( 20 ), "durable canonical winner survived every peer recreation", nullptr );
    StopPeer( first ); StopPeer( second ); StopPeer( third ); StopPeer( passive );
}

TEST_F( FinalityFaultNetwork, LateContenderAndPassiveRecipientRemainReceiveOnly )
{
    sgns::GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier )
                                                    { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );
    auto first = StartPeer( "late-validator-one", 54501 );
    auto second = StartPeer( "late-validator-two", 54502 );
    auto third = StartPeer( "late-validator-three", 54503 );
    auto passive = StartPeer( "late-passive-recipient", 54504 );
    ASSERT_TRUE( first.consensus && second.consensus && third.consensus && passive.consensus );

    const std::array<Peer *, 4> peers{ &first, &second, &third, &passive };
    const auto update = RegistryUpdate( { &first, &second, &third } );
    for ( auto *peer : peers )
        ASSERT_TRUE( peer->blockchain->GetValidatorRegistry()->StoreRegistryUpdate( update ).has_value() );
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return first.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
               second.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
               third.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
               passive.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value();
    }, std::chrono::seconds( 5 ), "every isolated peer durably stored the validator registry", nullptr );

    auto winner = MintFor( first );
    auto loser = MintFor( first, 1 );
    ASSERT_TRUE( winner && loser );
    ASSERT_EQ( winner->GetSlotID(), loser->GetSlotID() );
    auto winner_subject = sgns::ConsensusManager::CreateNonceSubject(
        first.account->GetAddress(), winner->GetNonce(), winner->GetHash(), winner->SerializeToEmbeddedTransaction(),
        CommitmentFor( *winner ), sgns::UTXOWitness{} );
    auto loser_subject = sgns::ConsensusManager::CreateNonceSubject(
        first.account->GetAddress(), loser->GetNonce(), loser->GetHash(), loser->SerializeToEmbeddedTransaction(),
        CommitmentFor( *loser ), sgns::UTXOWitness{} );
    ASSERT_TRUE( winner_subject.has_value() && loser_subject.has_value() );
    auto winner_proposal = first.consensus->CreateProposal( winner_subject.value(), first.account->GetAddress(),
                                                             first.blockchain->GetValidatorRegistry()->GetRegistryCid(),
                                                             first.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
    auto loser_proposal = second.consensus->CreateProposal( loser_subject.value(), second.account->GetAddress(),
                                                            second.blockchain->GetValidatorRegistry()->GetRegistryCid(),
                                                            second.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
    ASSERT_TRUE( winner_proposal.has_value() && loser_proposal.has_value() );
    const auto canonical_slot = sgns::MultiNodeFinalityFaultTestAccess::SlotKey( winner_proposal.value() );
    ASSERT_EQ( canonical_slot, sgns::MultiNodeFinalityFaultTestAccess::SlotKey( loser_proposal.value() ) );

    // Named topology barrier: only validators join before the winning public
    // submission. The passive recipient remains disconnected until certificate
    // persistence and the post-active-vote late-submission boundary are proven.
    for ( auto *source : { &first, &second, &third } )
        for ( auto *target : { &first, &second, &third } )
            if ( source != target ) source->pubsub->AddPeers( { target->pubsub->GetInterfaceAddress() } );
    ASSERT_TRUE( first.consensus->SubmitProposal( winner_proposal.value() ).has_value() );
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return sgns::MultiNodeFinalityFaultTestAccess::VotePublications( first.consensus ) > 0 &&
               sgns::MultiNodeFinalityFaultTestAccess::VotePublications( second.consensus ) > 0 &&
               sgns::MultiNodeFinalityFaultTestAccess::VotePublications( third.consensus ) > 0;
    }, std::chrono::seconds( 15 ), "durable active-vote publications reached each validator", nullptr );

    ASSERT_TRUE( second.consensus->SubmitProposal( loser_proposal.value() ).has_value() );
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return sgns::MultiNodeFinalityFaultTestAccess::DurableActiveVoteProposalId( first.consensus, canonical_slot ) ==
                   winner_proposal.value().proposal_id() &&
               sgns::MultiNodeFinalityFaultTestAccess::DurableActiveVoteProposalId( second.consensus, canonical_slot ) ==
                   winner_proposal.value().proposal_id() &&
               sgns::MultiNodeFinalityFaultTestAccess::DurableActiveVoteProposalId( third.consensus, canonical_slot ) ==
                   winner_proposal.value().proposal_id();
    }, std::chrono::seconds( 10 ), "late contender did not replace any durable active vote", nullptr );
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return first.consensus->CheckCertificateForSlot( canonical_slot ) &&
               second.consensus->CheckCertificateForSlot( canonical_slot ) &&
               third.consensus->CheckCertificateForSlot( canonical_slot );
    }, std::chrono::seconds( 20 ), "initial winner reached the accepted authoritative certificate boundary", nullptr );

    ASSERT_TRUE( third.consensus->SubmitProposal( loser_proposal.value() ).has_value() );

    ConnectPeers( peers );
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return passive.consensus->CheckCertificateForSlot( canonical_slot ) &&
               sgns::MultiNodeFinalityFaultTestAccess::CertificateNotificationsReceived( passive.consensus ) > 0 &&
               sgns::MultiNodeFinalityFaultTestAccess::AcceptedCertificateReadbacks( passive.consensus ) > 0 &&
               sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *passive.transactions ) == 1;
    }, std::chrono::seconds( 20 ), "passive peer received notification then completed committed durable recovery", nullptr );
    EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::CertificateWriteAttempts( passive.consensus ), 0u );

    for ( auto *peer : peers )
    {
        const auto certificate = peer->consensus->GetCertificateBySlot( canonical_slot );
        ASSERT_TRUE( certificate.has_value() );
        EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::NonceTransactionHash( certificate.value().proposal() ), winner->GetHash() );
        EXPECT_TRUE( HasOnlyWinnerOutput( *peer, *winner, *loser ) );
        EXPECT_TRUE( HasBridgeMarker( *peer, *winner ) );
    }

    RestartPeer( first ); RestartPeer( second ); RestartPeer( third ); RestartPeer( passive );
    ASSERT_TRUE( first.consensus && second.consensus && third.consensus && passive.consensus );
    ConnectPeers( { &first, &second, &third, &passive } );
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return first.consensus->CheckCertificateForSlot( canonical_slot ) &&
               second.consensus->CheckCertificateForSlot( canonical_slot ) &&
               third.consensus->CheckCertificateForSlot( canonical_slot ) &&
               passive.consensus->CheckCertificateForSlot( canonical_slot ) &&
               HasOnlyWinnerOutput( first, *winner, *loser ) && HasOnlyWinnerOutput( second, *winner, *loser ) &&
               HasOnlyWinnerOutput( third, *winner, *loser ) && HasOnlyWinnerOutput( passive, *winner, *loser ) &&
               HasBridgeMarker( first, *winner ) && HasBridgeMarker( second, *winner ) &&
               HasBridgeMarker( third, *winner ) && HasBridgeMarker( passive, *winner );
    }, std::chrono::seconds( 20 ), "late contender remained absent after reopened-store recovery", nullptr );
    StopPeer( first ); StopPeer( second ); StopPeer( third ); StopPeer( passive );
}

TEST_F( FinalityFaultNetwork, RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce )
{
    sgns::GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier )
                                                    { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );

    {
        auto network = StartNetwork( "restart-vote", 54601 );
        ASSERT_TRUE( network.first.consensus && network.second.consensus && network.third.consensus && network.passive.consensus );
        StoreRegistry( network );
        ConnectPeers( Peers( network ) );

        auto winner = MintFor( network.first );
        auto loser  = MintFor( network.first, 1 );
        ASSERT_TRUE( winner && loser );
        auto subject = sgns::ConsensusManager::CreateNonceSubject(
            network.first.account->GetAddress(), winner->GetNonce(), winner->GetHash(), winner->SerializeToEmbeddedTransaction(),
            CommitmentFor( *winner ), sgns::UTXOWitness{} );
        ASSERT_TRUE( subject.has_value() );
        auto proposal = network.first.consensus->CreateProposal(
            subject.value(), network.first.account->GetAddress(), network.first.blockchain->GetValidatorRegistry()->GetRegistryCid(),
            network.first.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
        ASSERT_TRUE( proposal.has_value() );
        const auto slot = sgns::MultiNodeFinalityFaultTestAccess::SlotKey( proposal.value() );

        sgns::MultiNodeFinalityFaultTestAccess::ArmActiveVoteBarrier( network.first.consensus );
        ASSERT_TRUE( network.first.consensus->SubmitProposal( proposal.value() ).has_value() );
        ASSERT_WAIT_FOR_CONDITION( [&] {
            return sgns::MultiNodeFinalityFaultTestAccess::ActiveVoteBarrierEntered( network.first.consensus );
        }, std::chrono::seconds( 15 ), "first validator paused after durable active-vote persistence", nullptr );
        ASSERT_EQ( sgns::MultiNodeFinalityFaultTestAccess::DurableActiveVoteProposalId( network.first.consensus, slot ),
                   proposal.value().proposal_id() );

        RestartPeer( network.first );
        ASSERT_TRUE( network.first.consensus );
        ASSERT_EQ( sgns::MultiNodeFinalityFaultTestAccess::DurableActiveVoteProposalId( network.first.consensus, slot ),
                   proposal.value().proposal_id() );
        ConnectPeers( Peers( network ) );
        // The original isolated publisher was stopped before its normal vote
        // announcement; re-advertise the unchanged signed proposal through the
        // public route after reconnecting so peers can drive normal recovery.
        ASSERT_TRUE( network.first.consensus->SubmitProposal( proposal.value() ).has_value() );
        ASSERT_WAIT_FOR_CONDITION( [&] {
            return network.first.consensus->CheckCertificateForSlot( slot ) && network.second.consensus->CheckCertificateForSlot( slot ) &&
                   network.third.consensus->CheckCertificateForSlot( slot ) && network.passive.consensus->CheckCertificateForSlot( slot ) &&
                   sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *network.first.transactions ) == 1 &&
                   sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *network.second.transactions ) == 1 &&
                   sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *network.third.transactions ) == 1 &&
                   sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *network.passive.transactions ) == 1;
        }, std::chrono::seconds( 25 ), "recreated vote owner recovered only its durable vote before exact finality", nullptr );
        for ( auto *peer : Peers( network ) ) AssertSingleDurableMint( *peer, slot, *winner, *loser, 1 );

        RestartAndReconnect( network );
        ASSERT_WAIT_FOR_CONDITION( [&] {
            return network.first.consensus->CheckCertificateForSlot( slot ) && network.second.consensus->CheckCertificateForSlot( slot ) &&
                   network.third.consensus->CheckCertificateForSlot( slot ) && network.passive.consensus->CheckCertificateForSlot( slot ) &&
                   HasOnlyWinnerOutput( network.first, *winner, *loser ) && HasOnlyWinnerOutput( network.second, *winner, *loser ) &&
                   HasOnlyWinnerOutput( network.third, *winner, *loser ) && HasOnlyWinnerOutput( network.passive, *winner, *loser ) &&
                   HasBridgeMarker( network.first, *winner ) && HasBridgeMarker( network.second, *winner ) &&
                   HasBridgeMarker( network.third, *winner ) && HasBridgeMarker( network.passive, *winner );
        }, std::chrono::seconds( 20 ), "vote-boundary finality remained exact after reopening every durable root", nullptr );
        for ( auto *peer : Peers( network ) ) AssertSingleDurableMint( *peer, slot, *winner, *loser, 0 );
        StopNetwork( network );
    }

    {
        auto network = StartNetwork( "restart-certificate", 54611 );
        ASSERT_TRUE( network.first.consensus && network.second.consensus && network.third.consensus && network.passive.consensus );
        StoreRegistry( network );
        ConnectPeers( Peers( network ) );

        auto winner = MintFor( network.first );
        auto loser  = MintFor( network.first, 1 );
        ASSERT_TRUE( winner && loser );
        auto subject = sgns::ConsensusManager::CreateNonceSubject(
            network.first.account->GetAddress(), winner->GetNonce(), winner->GetHash(), winner->SerializeToEmbeddedTransaction(),
            CommitmentFor( *winner ), sgns::UTXOWitness{} );
        ASSERT_TRUE( subject.has_value() );
        auto proposal = network.first.consensus->CreateProposal(
            subject.value(), network.first.account->GetAddress(), network.first.blockchain->GetValidatorRegistry()->GetRegistryCid(),
            network.first.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
        ASSERT_TRUE( proposal.has_value() );
        const auto slot = sgns::MultiNodeFinalityFaultTestAccess::SlotKey( proposal.value() );

        sgns::MultiNodeFinalityFaultTestAccess::ArmAcceptedCertificateBarrier( network.second.consensus );
        ASSERT_TRUE( network.first.consensus->SubmitProposal( proposal.value() ).has_value() );
        ASSERT_WAIT_FOR_CONDITION( [&] {
            return sgns::MultiNodeFinalityFaultTestAccess::AcceptedCertificateBarrierEntered( network.second.consensus );
        }, std::chrono::seconds( 25 ), "second validator paused after accepted certificate durable readback", nullptr );

        RestartPeer( network.second );
        ASSERT_TRUE( network.second.consensus );
        ConnectPeers( Peers( network ) );
        ASSERT_WAIT_FOR_CONDITION( [&] {
            return network.first.consensus->CheckCertificateForSlot( slot ) && network.second.consensus->CheckCertificateForSlot( slot ) &&
                   network.third.consensus->CheckCertificateForSlot( slot ) && network.passive.consensus->CheckCertificateForSlot( slot ) &&
                   sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *network.first.transactions ) == 1 &&
                   sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *network.second.transactions ) == 1 &&
                   sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *network.third.transactions ) == 1 &&
                   sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *network.passive.transactions ) == 1;
        }, std::chrono::seconds( 25 ), "recreated certificate recipient recovered the exact durable winner", nullptr );
        for ( auto *peer : Peers( network ) ) AssertSingleDurableMint( *peer, slot, *winner, *loser, 1 );

        RestartAndReconnect( network );
        ASSERT_WAIT_FOR_CONDITION( [&] {
            return HasOnlyWinnerOutput( network.first, *winner, *loser ) && HasOnlyWinnerOutput( network.second, *winner, *loser ) &&
                   HasOnlyWinnerOutput( network.third, *winner, *loser ) && HasOnlyWinnerOutput( network.passive, *winner, *loser ) &&
                   HasBridgeMarker( network.first, *winner ) && HasBridgeMarker( network.second, *winner ) &&
                   HasBridgeMarker( network.third, *winner ) && HasBridgeMarker( network.passive, *winner );
        }, std::chrono::seconds( 20 ), "certificate-boundary recovery remained exact after reopening every durable root", nullptr );
        for ( auto *peer : Peers( network ) ) AssertSingleDurableMint( *peer, slot, *winner, *loser, 0 );
        StopNetwork( network );
    }

    {
        auto network = StartNetwork( "restart-mint", 54621 );
        ASSERT_TRUE( network.first.consensus && network.second.consensus && network.third.consensus && network.passive.consensus );
        StoreRegistry( network );
        ConnectPeers( Peers( network ) );

        auto winner = MintFor( network.first );
        auto loser  = MintFor( network.first, 1 );
        ASSERT_TRUE( winner && loser );
        auto subject = sgns::ConsensusManager::CreateNonceSubject(
            network.first.account->GetAddress(), winner->GetNonce(), winner->GetHash(), winner->SerializeToEmbeddedTransaction(),
            CommitmentFor( *winner ), sgns::UTXOWitness{} );
        ASSERT_TRUE( subject.has_value() );
        auto proposal = network.first.consensus->CreateProposal(
            subject.value(), network.first.account->GetAddress(), network.first.blockchain->GetValidatorRegistry()->GetRegistryCid(),
            network.first.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
        ASSERT_TRUE( proposal.has_value() );
        const auto slot = sgns::MultiNodeFinalityFaultTestAccess::SlotKey( proposal.value() );

        sgns::MultiNodeFinalityFaultTestAccess::ArmMintEffectsBarrier( network.first.transactions );
        ASSERT_TRUE( network.first.consensus->SubmitProposal( proposal.value() ).has_value() );
        ASSERT_WAIT_FOR_CONDITION( [&] {
            return sgns::MultiNodeFinalityFaultTestAccess::MintEffectsBarrierEntered( network.first.transactions );
        }, std::chrono::seconds( 25 ), "first validator paused after durable Mint effects and before bridge marker", nullptr );
        EXPECT_TRUE( HasOnlyWinnerOutput( network.first, *winner, *loser ) );
        EXPECT_FALSE( HasBridgeMarker( network.first, *winner ) );

        RestartPeer( network.first );
        ASSERT_TRUE( network.first.consensus );
        ConnectPeers( Peers( network ) );
        ASSERT_WAIT_FOR_CONDITION( [&] {
            return network.first.consensus->CheckCertificateForSlot( slot ) && network.second.consensus->CheckCertificateForSlot( slot ) &&
                   network.third.consensus->CheckCertificateForSlot( slot ) && network.passive.consensus->CheckCertificateForSlot( slot ) &&
                   sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *network.first.transactions ) == 1 &&
                   sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *network.second.transactions ) == 1 &&
                   sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *network.third.transactions ) == 1 &&
                   sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *network.passive.transactions ) == 1 &&
                   HasBridgeMarker( network.first, *winner );
        }, std::chrono::seconds( 25 ), "recreated Mint peer repaired its marker through normal certificate recovery", nullptr );
        for ( auto *peer : Peers( network ) ) AssertSingleDurableMint( *peer, slot, *winner, *loser, 1 );

        RestartAndReconnect( network );
        ASSERT_WAIT_FOR_CONDITION( [&] {
            return HasOnlyWinnerOutput( network.first, *winner, *loser ) && HasOnlyWinnerOutput( network.second, *winner, *loser ) &&
                   HasOnlyWinnerOutput( network.third, *winner, *loser ) && HasOnlyWinnerOutput( network.passive, *winner, *loser ) &&
                   HasBridgeMarker( network.first, *winner ) && HasBridgeMarker( network.second, *winner ) &&
                   HasBridgeMarker( network.third, *winner ) && HasBridgeMarker( network.passive, *winner );
        }, std::chrono::seconds( 20 ), "Mint-boundary recovery stayed exact after reopening every durable root", nullptr );
        for ( auto *peer : Peers( network ) ) AssertSingleDurableMint( *peer, slot, *winner, *loser, 0 );
        StopNetwork( network );
    }
}
