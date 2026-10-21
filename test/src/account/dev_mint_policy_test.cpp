/**
 * @file       dev_mint_policy_test.cpp
 * @brief      D-03 net-scoped dev-mint policy proofs: the evidence bypass legs
 *             permit burn-less mints only on designated nets, the dev
 *             validator's verdict follows the validator's OWN net regardless
 *             of witness content, and a "dev"-chain mint routes through the
 *             existing input-validator registry.
 * @details    The process-global net id (version::GetNetworkID) defaults to
 *             DEV_NET_ID 144 (designated), so every fixture that switches nets
 *             saves the current id in SetUp and restores it in TearDown — the
 *             id is shared by every suite in this binary. The evidence-bypass
 *             suites drive PublicChainInputValidator::GatherVerificationEvidence
 *             through the slot-test friend-accessor pattern; the validator
 *             suites use a real Blockchain reference (the verdict call shape)
 *             on the CRDTFixture stack.
 * @date       2026-10-21
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/filesystem.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <libp2p/basic/scheduler/scheduler_impl.hpp>

#include "account/DevMintInputValidator.hpp"
#include "account/GeniusAccount.hpp"
#include "account/InputValidators.hpp"
#include "account/MintTransactionV2.hpp"
#include "account/PublicChainInputValidator.hpp"
#include "account/UTXOMerkle.hpp"
#include "base/DevMintPolicy.hpp"
#include "base/sgns_version.hpp"
#include "blockchain/Blockchain.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "crdt/crdt_options.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/wait_condition.hpp"
#include "transaction/TransactionManager.hpp"

using namespace sgns;

/// @brief Friend accessor for the private evidence-gathering path (the
///        public_chain_input_validator_slot_test pattern, per-binary definition).
class PublicChainInputValidatorTestAccess
{
public:
    static RpcVerificationEvidence Gather( const PublicChainInputValidator &validator,
                                           const GeniusTransaction          &tx,
                                           const std::string                &source_reference )
    {
        return validator.GatherVerificationEvidence( tx, source_reference );
    }
};

namespace
{
    const TokenID kDevMintToken = TokenID::FromBytes( { 0x00 } );

    /// Fabricated burn hash (the multi_node_finality_fault_test shape): no
    /// public chain ever backs it.
    const std::string kBurnHashReadable = std::string( 64, 'c' );

    /// A non-empty external source reference, shaped like a public-chain tx hash.
    const std::string kSourceReference = "0x" + std::string( 64, 'b' );

    std::shared_ptr<MintTransactionV2> MintWithChain( std::string chain_id )
    {
        SGTransaction::DAGStruct dag;
        dag.set_type( "mint-v2" );
        const auto burn_hash = base::Hash256::fromReadableString( kBurnHashReadable );
        dag.set_uncle_hash( burn_hash.value().toReadableString() );
        return std::make_shared<MintTransactionV2>( MintTransactionV2::New(
            42, std::move( chain_id ), kDevMintToken, std::move( dag ), { { burn_hash.value(), 0, {} } },
            "dev-mint-destination" ) );
    }
} // namespace

/**
 * @brief Single-node fixture for the D-03 unit proofs. Saves the process-global
 *        net id in SetUp and restores it in TearDown so the per-test net
 *        switching never leaks into other suites sharing this binary.
 */
class DevMintPolicyUnitTest : public ::test::CRDTFixture
{
public:
    DevMintPolicyUnitTest() : ::test::CRDTFixture( "dev_mint_policy_test" )
    {
    }

    void SetUp() override
    {
        saved_net_ = version::GetNetworkID();
        GeniusAccount::SetSecureStorageFactory(
            []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage> {
                return std::make_shared<MemorySecureStorage>( identifier );
            } );
        account_ = GeniusAccount::New( kDevMintToken, base_path / "account" );
        ASSERT_NE( account_, nullptr );
        blockchain_ = Blockchain::New( db_, account_, pubs_, []( outcome::result<void> ) {} );
        ASSERT_NE( blockchain_, nullptr );
    }

    void TearDown() override
    {
        version::SetNetworkId( saved_net_ );
    }

    ~DevMintPolicyUnitTest() override
    {
        if ( blockchain_ )
        {
            (void) blockchain_->Stop();
        }
        blockchain_.reset();
        account_.reset();
    }

    IInputValidator::WitnessVerdict DevValidatorVerdictFor( const GeniusTransaction &tx ) const
    {
        const DevMintInputValidator validator;
        const ConsensusSubject      subject;
        const UTXOTxParameters      params;
        return validator.ValidateWitness( subject, tx, params, *blockchain_ );
    }

    uint16_t                       saved_net_ = version::DEV_NET_ID;
    std::shared_ptr<GeniusAccount> account_;
    std::shared_ptr<Blockchain>    blockchain_;
};

// ── Policy predicate: exactly the designated nets ──────────────────────────

TEST_F( DevMintPolicyUnitTest, PolicyPermitsExactlyTheDesignatedNets )
{
    for ( const auto permitted : { version::STAGING_NET_ID, version::DEV_NET_ID } )
    {
        version::SetNetworkId( permitted );
        EXPECT_TRUE( DevMintBurnlessPermitted() ) << "net " << permitted << " must permit dev mints";
    }
    for ( const auto refused : { version::TEST_NET_ID, version::MAIN_NET_ID } )
    {
        version::SetNetworkId( refused );
        EXPECT_FALSE( DevMintBurnlessPermitted() ) << "net " << refused << " must refuse dev mints";
    }
}

// ── Evidence bypass: both legs net-scoped, default-invalid on refusal ──────

TEST_F( DevMintPolicyUnitTest, EvidenceBypassEmptySourceReferencePermitsOnlyOnDesignatedNets )
{
    const PublicChainInputValidator validator; // no RPC endpoints wired
    const auto                      mint = MintWithChain( "dev" );

    for ( const auto designated : { version::STAGING_NET_ID, version::DEV_NET_ID } )
    {
        version::SetNetworkId( designated );
        const auto evidence = PublicChainInputValidatorTestAccess::Gather( validator, *mint, "" );
        EXPECT_TRUE( evidence.valid ) << "net " << designated;
    }
    for ( const auto refused : { version::TEST_NET_ID, version::MAIN_NET_ID } )
    {
        version::SetNetworkId( refused );
        const auto evidence = PublicChainInputValidatorTestAccess::Gather( validator, *mint, "" );
        EXPECT_FALSE( evidence.valid ) << "net " << refused;
    }
}

TEST_F( DevMintPolicyUnitTest, EvidenceBypassEmptyChainIdPermitsOnlyOnDesignatedNets )
{
    const PublicChainInputValidator validator; // no RPC endpoints wired
    const auto                      mint = MintWithChain( "" ); // the actually-reachable hole

    for ( const auto designated : { version::STAGING_NET_ID, version::DEV_NET_ID } )
    {
        version::SetNetworkId( designated );
        const auto evidence = PublicChainInputValidatorTestAccess::Gather( validator, *mint, kSourceReference );
        EXPECT_TRUE( evidence.valid ) << "net " << designated;
    }
    for ( const auto refused : { version::TEST_NET_ID, version::MAIN_NET_ID } )
    {
        version::SetNetworkId( refused );
        const auto evidence = PublicChainInputValidatorTestAccess::Gather( validator, *mint, kSourceReference );
        EXPECT_FALSE( evidence.valid ) << "net " << refused;
        EXPECT_TRUE( evidence.successful_public.empty() ) << "refusal must not fabricate endpoint slots";
    }
}

TEST_F( DevMintPolicyUnitTest, EvidenceBypassSupergeniusChainIdPermitsOnlyOnDesignatedNets )
{
    const PublicChainInputValidator validator; // no RPC endpoints wired
    const auto                      mint = MintWithChain( "supergenius" );

    for ( const auto designated : { version::STAGING_NET_ID, version::DEV_NET_ID } )
    {
        version::SetNetworkId( designated );
        const auto evidence = PublicChainInputValidatorTestAccess::Gather( validator, *mint, kSourceReference );
        EXPECT_TRUE( evidence.valid ) << "net " << designated;
    }
    for ( const auto refused : { version::TEST_NET_ID, version::MAIN_NET_ID } )
    {
        version::SetNetworkId( refused );
        const auto evidence = PublicChainInputValidatorTestAccess::Gather( validator, *mint, kSourceReference );
        EXPECT_FALSE( evidence.valid ) << "net " << refused;
    }
}

// ── Dev validator: verdict follows the validator's OWN net ─────────────────

TEST_F( DevMintPolicyUnitTest, DevValidatorWitnessVerdictFollowsOwnNetRegardlessOfWitnessContent )
{
    const auto mint = MintWithChain( "dev" ); // identical witness bytes for every net

    for ( const auto designated : { version::STAGING_NET_ID, version::DEV_NET_ID } )
    {
        version::SetNetworkId( designated );
        EXPECT_EQ( DevValidatorVerdictFor( *mint ), IInputValidator::WitnessVerdict::kValid )
            << "net " << designated;
    }
    for ( const auto refused : { version::TEST_NET_ID, version::MAIN_NET_ID } )
    {
        version::SetNetworkId( refused );
        EXPECT_EQ( DevValidatorVerdictFor( *mint ), IInputValidator::WitnessVerdict::kInvalid )
            << "net " << refused;
    }
}

TEST_F( DevMintPolicyUnitTest, DevValidatorRequiresNoConsensusUtxoDataOnEveryNet )
{
    const DevMintInputValidator validator;
    EXPECT_FALSE( validator.RequiresConsensusUTXOData() );
    version::SetNetworkId( version::TEST_NET_ID );
    EXPECT_FALSE( validator.RequiresConsensusUTXOData() );
    version::SetNetworkId( version::MAIN_NET_ID );
    EXPECT_FALSE( validator.RequiresConsensusUTXOData() );
}

TEST_F( DevMintPolicyUnitTest, DevValidatorAcceptsNonEmptyUtxoParameters )
{
    const DevMintInputValidator validator;
    const auto                  mint   = MintWithChain( "dev" );
    const auto                  params = mint->GetUTXOParameters();
    EXPECT_FALSE( params.first.empty() );
    EXPECT_FALSE( params.second.empty() );
    EXPECT_TRUE( validator.ValidateUTXOParameters( params, "dev-mint-source", account_->GetUTXOManager() ) );
}

TEST_F( DevMintPolicyUnitTest, DevChainMintRoutesToDevValidatorThroughRegistry )
{
    // Routing-order proof: no RPC endpoint claims "dev" (SelectInputValidator
    // step 1 misses) and the process-global registry (step 2) resolves the
    // chain id to a validator whose verdict follows the net policy.
    const PublicChainInputValidator public_validator;
    EXPECT_FALSE( public_validator.GetFirstRpcUrl( "dev" ).has_value() );

    const auto *const registered = IInputValidator::Get( "dev" );
    ASSERT_NE( registered, nullptr ) << "the dev validator must be registered for the literal chain id \"dev\"";

    const auto             mint = MintWithChain( "dev" );
    const ConsensusSubject subject;
    const UTXOTxParameters params;

    version::SetNetworkId( version::STAGING_NET_ID );
    EXPECT_EQ( registered->ValidateWitness( subject, *mint, params, *blockchain_ ),
               IInputValidator::WitnessVerdict::kValid );
    version::SetNetworkId( version::MAIN_NET_ID );
    EXPECT_EQ( registered->ValidateWitness( subject, *mint, params, *blockchain_ ),
               IInputValidator::WitnessVerdict::kInvalid );
}

// ── Real-consensus suites (multi_node_finality_fault_test harness shape) ───

/**
 * @brief Four-peer real-consensus fixture: real GossipPubSub, real GlobalDB,
 *        real quorum voting. The net id is switched BEFORE any peer is
 *        constructed (consensus and datastore topics carry the net appendix)
 *        and restored in TearDown — the id is process-global, so every other
 *        suite in this binary must see the pre-test value again.
 */
class DevMintConsensusTest : public ::test::CRDTFixture
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
        std::thread                                      io_thread;

        Peer()                              = default;
        Peer( const Peer & )                = delete;
        Peer &operator=( const Peer & )     = delete;
        Peer( Peer && ) noexcept            = default;
        Peer &operator=( Peer && ) noexcept = default;

        ~Peer()
        {
            Stop();
        }

        void Stop() noexcept
        {
            if ( transactions )
            {
                transactions->Stop();
            }
            transactions.reset();
            if ( blockchain )
            {
                (void) blockchain->Stop(); // closes the consensus manager internally
            }
            blockchain.reset();
            if ( io )
            {
                io->stop();
            }
            if ( io_thread.joinable() )
            {
                io_thread.join();
            }
            /*
             * Teardown invariant (asio), copied from the multi-node finality
             * fixture: GlobalDB's graphsync chain co-owns the libp2p host wired
             * from pubsub->GetHost() at StartPeer, and the account holds
             * db-backed handles, so BOTH must be released BEFORE pubsub->Stop()
             * — StopImpl destroys m_host/m_context while external co-owners
             * keep the host alive, and the later ~BasicHost deregisters leftover
             * TcpConnections from the freed kqueue reactor.
             */
            db.reset();
            account.reset();
            if ( pubsub )
            {
                pubsub->Stop();
            }
            pubsub.reset();
            io.reset();
        }

        bool ConnectedTo( Peer &other )
        {
            if ( !pubsub || !other.pubsub || !pubsub->GetHost() || !other.pubsub->GetHost() )
            {
                return false;
            }
            return pubsub->GetHost()->connectedness( other.pubsub->GetHost()->getPeerInfo() ) ==
                       libp2p::Host::Connectedness::CONNECTED ||
                   other.pubsub->GetHost()->connectedness( pubsub->GetHost()->getPeerInfo() ) ==
                       libp2p::Host::Connectedness::CONNECTED;
        }
    };

    struct Network
    {
        Peer first;
        Peer second;
        Peer third;
        Peer passive;
    };

public:
    DevMintConsensusTest() : ::test::CRDTFixture( "dev_mint_policy_test" )
    {
    }

    void SetUp() override
    {
        saved_net_ = version::GetNetworkID();
        GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                                                { return std::make_shared<MemorySecureStorage>( identifier ); } );
    }

    void TearDown() override
    {
        StopNetwork();
        version::SetNetworkId( saved_net_ );
    }

    ~DevMintConsensusTest() override
    {
        StopNetwork();
    }

    /// @brief Starts the four-peer network on @p net. Must run before any
    ///        per-test net switching: peers are constructed under this net.
    void StartNetworkOn( uint16_t net, const std::string &prefix )
    {
        version::SetNetworkId( net );
        network_         = Network{ StartPeer( prefix + "-validator-one", kPeerPorts[0] ),
                            StartPeer( prefix + "-validator-two", kPeerPorts[1] ),
                            StartPeer( prefix + "-validator-three", kPeerPorts[2] ),
                            StartPeer( prefix + "-passive", kPeerPorts[3] ) };
        network_started_ = true;
        ASSERT_TRUE( network_.first.blockchain && network_.second.blockchain && network_.third.blockchain &&
                     network_.passive.blockchain );
        ConnectPeers();
        StoreRegistry();
    }

    std::array<Peer *, 4> Peers()
    {
        return { &network_.first, &network_.second, &network_.third, &network_.passive };
    }

    static std::shared_ptr<MintTransactionV2> MintFor( const Peer &peer, std::string chain_id )
    {
        SGTransaction::DAGStruct dag;
        dag.set_type( "mint-v2" );
        dag.set_source_addr( peer.account->GetAddress() );
        dag.set_nonce( 0 );
        dag.set_timestamp(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );
        const auto burn_hash = base::Hash256::fromReadableString( kBurnHashReadable );
        EXPECT_TRUE( burn_hash.has_value() );
        dag.set_uncle_hash( burn_hash.value().toReadableString() );
        auto mint = std::make_shared<MintTransactionV2>( MintTransactionV2::New( 42,
                                                                                 std::move( chain_id ),
                                                                                 kDevMintToken,
                                                                                 std::move( dag ),
                                                                                 { { burn_hash.value(), 0, {} } },
                                                                                 peer.account->GetAddress() ) );
        mint->MakeSignature( *peer.account );
        return mint;
    }

    /// @brief Params-only merkle commitment (the multi_node_finality shape).
    static UTXOTransitionCommitment CommitmentFor( const MintTransactionV2 &mint )
    {
        UTXOTransitionCommitment          commitment;
        const auto                        params = mint.GetUTXOParameters();
        std::vector<std::vector<uint8_t>> consumed_payloads;
        for ( const auto &input : params.first )
        {
            auto *consumed = commitment.add_consumed_outpoints();
            consumed->set_tx_id_hash( input.txid_hash_.data(), input.txid_hash_.size() );
            consumed->set_output_index( input.output_idx_ );

            std::vector<uint8_t> payload( input.txid_hash_.begin(), input.txid_hash_.end() );
            utxo_merkle::AppendUInt32BE( payload, input.output_idx_ );
            consumed_payloads.push_back( std::move( payload ) );
        }

        const auto mint_hash = base::Hash256::fromReadableString( mint.GetHash() );
        EXPECT_TRUE( mint_hash.has_value() );
        std::vector<std::vector<uint8_t>> produced_payloads;
        for ( uint32_t index = 0; index < params.second.size(); ++index )
        {
            const auto &output = params.second[index];
            GeniusUTXO  produced( mint_hash.value(),
                                 index,
                                 output.encrypted_amount,
                                 output.token_id,
                                 output.dest_address );
            auto       *committed = commitment.add_produced_outputs();
            committed->set_tx_id_hash( mint_hash.value().data(), mint_hash.value().size() );
            committed->set_output_index( index );
            committed->set_owner_address( output.dest_address );
            const auto token_bytes = output.token_id.bytes();
            committed->set_token_id( token_bytes.data(), token_bytes.size() );
            committed->set_amount( output.encrypted_amount );
            produced_payloads.push_back( utxo_merkle::SerializeUTXOLeafPayload( produced ) );
        }

        const auto consumed_root = utxo_merkle::ComputeMerkleRootFromPayloads( std::move( consumed_payloads ) );
        const auto produced_root = utxo_merkle::ComputeMerkleRootFromPayloads( std::move( produced_payloads ) );
        commitment.set_consumed_outpoints_root( consumed_root.data(), consumed_root.size() );
        commitment.set_produced_outputs_root( produced_root.data(), produced_root.size() );
        return commitment;
    }

    /// @brief Proposes @p mint from the first validator through the public
    ///        Blockchain surface; returns the canonical slot key.
    std::string ProposeMint( const Peer &proposer, const MintTransactionV2 &mint )
    {
        auto proposal = proposer.blockchain->CreateConsensusProposal( proposer.account->GetAddress(),
                                                                      mint.GetNonce(),
                                                                      mint.GetHash(),
                                                                      mint.SerializeToEmbeddedTransaction(),
                                                                      CommitmentFor( mint ),
                                                                      UTXOWitness{} );
        EXPECT_TRUE( proposal.has_value() );
        if ( proposal.has_error() )
        {
            return {};
        }
        const auto slot = ConsensusManager::GetSlotKey( proposal.value() );
        EXPECT_FALSE( slot.empty() );
        EXPECT_TRUE( proposer.blockchain->SubmitProposal( proposal.value() ).has_value() );
        return slot;
    }

    static bool MintedUtxoLanded( const Peer &peer, const MintTransactionV2 &mint )
    {
        const auto outputs = peer.account->GetUTXOManager().GetUTXOs( mint.dag_st.source_addr() );
        return outputs.size() == 1 && outputs.front().GetTxID().toReadableString() == mint.GetHash();
    }

    uint16_t saved_net_ = version::DEV_NET_ID;
    Network  network_;
    bool     network_started_ = false;

private:
    Peer StartPeer( const std::string &name, uint16_t port )
    {
        Peer peer;
        peer.name = name;
        peer.port = port;
        peer.root = ( base_path / name ).string();
        boost::filesystem::create_directories( peer.root );
        peer.io      = std::make_shared<boost::asio::io_context>();
        auto keypair = sgns::crdt::KeyPairFileStorage( peer.root + "/keypair" ).GetKeyPair();
        EXPECT_TRUE( keypair.has_value() );
        if ( keypair.has_error() )
        {
            return peer;
        }
        peer.pubsub = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>( keypair.value() );
        EXPECT_TRUE( peer.pubsub );
        if ( !peer.pubsub )
        {
            return peer;
        }
        // Advertise the concrete IPv4 address discovered by the fixture's
        // ephemeral listener (wildcard-only listeners make every broadcast
        // silently drop), but keep the LISTEN on the wildcard.
        const auto fixture_address = libp2p::multi::Multiaddress::create( pubs_->GetLocalAddress() );
        EXPECT_TRUE( fixture_address.has_value() );
        if ( fixture_address.has_error() )
        {
            return peer;
        }
        const auto bind_address = fixture_address.value().getFirstValueForProtocol(
            libp2p::multi::Protocol::Code::IP4 );
        EXPECT_TRUE( bind_address.has_value() );
        if ( !bind_address.has_value() )
        {
            return peer;
        }
        EXPECT_FALSE(
            peer.pubsub->Start( port, {}, /*bindAddresses=*/"", /*addAddresses=*/{ bind_address.value() } ).get() );
        EXPECT_FALSE( peer.pubsub->GetHost()->getPeerInfo().addresses.empty() );
        // GraphSync writes to libp2p streams from its scheduler thread, and
        // libp2p is single-threaded per host, so the scheduler runs on the
        // host's io_context (the multi-node finality fixture's split).
        auto scheduler = std::make_shared<libp2p::basic::SchedulerImpl>(
            std::make_shared<libp2p::basic::AsioSchedulerBackend>( peer.pubsub->GetAsioContext() ),
            libp2p::basic::Scheduler::Config{ std::chrono::milliseconds( 100 ) } );
        auto graphsync = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( peer.pubsub->GetHost(),
                                                                                      scheduler );
        auto generator = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();
        auto db        = sgns::crdt::GlobalDB::New( peer.io,
                                             peer.root + "/rocksdb",
                                             peer.pubsub,
                                             sgns::crdt::CrdtOptions::DefaultOptions(),
                                             graphsync,
                                             scheduler,
                                             generator );
        EXPECT_TRUE( db.has_value() );
        if ( db.has_error() )
        {
            return peer;
        }
        peer.db = std::move( db.value() );
        peer.db->Start();
        peer.io_thread = std::thread(
            [io = peer.io]
            {
                auto guard = boost::asio::make_work_guard( *io );
                io->run();
            } );
        peer.account = sgns::GeniusAccount::New( kDevMintToken, peer.root + "/account" );
        EXPECT_TRUE( peer.account );
        if ( !peer.account )
        {
            return peer;
        }
        EXPECT_TRUE( peer.account->GetUTXOManager().LoadUTXOs( peer.db->GetDataStore() ).has_value() );
        peer.blockchain = sgns::Blockchain::New( peer.db, peer.account, peer.pubsub, []( outcome::result<void> ) {} );
        EXPECT_TRUE( peer.blockchain );
        if ( !peer.blockchain )
        {
            return peer;
        }
        peer.transactions = sgns::TransactionManager::New( peer.db,
                                                           peer.io,
                                                           peer.account,
                                                           peer.blockchain,
                                                           sgns::NodeType::Light );
        EXPECT_TRUE( peer.transactions );
        return peer;
    }

    void StopNetwork() noexcept
    {
        if ( network_started_ )
        {
            network_started_ = false;
            network_.first.Stop();
            network_.second.Stop();
            network_.third.Stop();
            network_.passive.Stop();
        }
    }

    /// @brief Flood-fill reachability over pairwise host connectedness: with
    ///        four peers, one connected component proves every consensus-topic
    ///        subscriber can hear every other (the analog's topology check).
    static bool FormsConnectedTopology( const std::array<Peer *, 4> &peers )
    {
        std::array<bool, 4> reachable{};
        reachable.front() = true;
        for ( size_t pass = 0; pass < reachable.size(); ++pass )
        {
            for ( size_t source_index = 0; source_index < reachable.size(); ++source_index )
            {
                if ( !reachable[source_index] )
                {
                    continue;
                }
                for ( size_t target_index = 0; target_index < reachable.size(); ++target_index )
                {
                    if ( reachable[target_index] || source_index == target_index )
                    {
                        continue;
                    }
                    if ( peers[source_index]->ConnectedTo( *peers[target_index] ) )
                    {
                        reachable[target_index] = true;
                    }
                }
            }
        }
        return std::all_of( reachable.begin(), reachable.end(), []( bool connected ) { return connected; } );
    }

    void ConnectPeers()
    {
        const auto peers = Peers();
        // Join one source per round, spaced past the 100ms gossip heartbeat:
        // simultaneous dials from fixed listen ports collide on the shared
        // tuple (the SYN-collision lesson from the multi-node finality suite).
        for ( auto *source : peers )
        {
            for ( auto *target : peers )
            {
                if ( source != target )
                {
                    source->pubsub->AddPeers( { target->pubsub->GetInterfaceAddress() } );
                }
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 300 ) );
        }
        ASSERT_WAIT_FOR_CONDITION( [&] { return FormsConnectedTopology( peers ); },
                                   std::chrono::seconds( 5 ),
                                   "every peer is started in one connected libp2p topology",
                                   nullptr );
    }

    void StoreRegistry()
    {
        const std::array<Peer *, 3>       validators{ &network_.first, &network_.second, &network_.third };
        ValidatorRegistry::RegistryUpdate update;
        update.mutable_registry()->set_epoch( 1 );
        for ( auto *peer : validators )
        {
            auto *validator = update.mutable_registry()->add_validators();
            validator->set_validator_id( peer->account->GetAddress() );
            validator->set_weight( 1 );
            validator->set_role( ValidatorRegistry::Role::REGULAR );
            validator->set_status( ValidatorRegistry::Status::ACTIVE );
        }
        validator::RegistrySigningPayload payload;
        *payload.mutable_registry() = update.registry();
        std::string bytes;
        ASSERT_TRUE( payload.SerializeToString( &bytes ) );
        auto  signature = validators.front()->account->Sign( std::vector<uint8_t>( bytes.begin(), bytes.end() ) );
        auto *entry     = update.add_signatures();
        entry->set_validator_id( validators.front()->account->GetAddress() );
        entry->set_signature( signature.data(), signature.size() );

        for ( auto *peer : Peers() )
        {
            ASSERT_TRUE( peer->blockchain->GetValidatorRegistry()->StoreRegistryUpdate( update ).has_value() );
        }
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                return network_.first.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
                       network_.second.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
                       network_.third.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
                       network_.passive.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value();
            },
            std::chrono::seconds( 5 ),
            "every peer durably stored the validator registry",
            nullptr );
    }

    static constexpr std::array<uint16_t, 4> kPeerPorts{ 54601, 54602, 54603, 54604 };
};

// ── Designated net: full consensus certifies the burn-less dev mint ────────

TEST_F( DevMintConsensusTest, DesignatedNetCertifiesDevChainBurnlessMintThroughRealQuorum )
{
    // Net 333 (staging) is designated: the dev validator returns kValid, so a
    // genuine quorum votes and a certificate lands through CRDT.
    StartNetworkOn( version::STAGING_NET_ID, "designated" );

    const auto mint = MintFor( network_.first, "dev" );
    ASSERT_TRUE( mint );
    const auto slot = ProposeMint( network_.first, *mint );
    ASSERT_FALSE( slot.empty() );

    for ( auto *peer : Peers() )
    {
        ASSERT_WAIT_FOR_CONDITION( [&] { return peer->blockchain->CheckCertificateForSlot( slot ); },
                                   std::chrono::seconds( 20 ),
                                   "the dev-chain burn-less mint certified through real quorum and CRDT",
                                   nullptr );
    }

    // The certificate binds exactly this mint.
    const auto certificate = network_.first.blockchain->GetCertificateBySlot( slot );
    ASSERT_TRUE( certificate.has_value() );
    const auto nonce_subject = ConsensusManager::DecodeNonceSubject( certificate.value().proposal().subject() );
    ASSERT_TRUE( nonce_subject.has_value() );
    EXPECT_EQ( nonce_subject.value().tx_hash(), mint->GetHash() );

    // The mint's produced UTXO landed on every peer (spendable history exists).
    for ( auto *peer : Peers() )
    {
        ASSERT_WAIT_FOR_CONDITION( [&] { return MintedUtxoLanded( *peer, *mint ); },
                                   std::chrono::seconds( 20 ),
                                   "the minted UTXO landed on the peer",
                                   nullptr );
    }
}

// ── Non-designated net: the identical mint never certifies ─────────────────

TEST_F( DevMintConsensusTest, RefusesOnNonDesignatedNetDevChainMintNeverCertifies )
{
    // Net 963 (test): the SAME subject shape as the designated case — the
    // validators' own net refuses the burn-less mint regardless of the
    // proposer's declaration.
    StartNetworkOn( version::TEST_NET_ID, "refuses-dev" );

    const auto mint = MintFor( network_.first, "dev" );
    ASSERT_TRUE( mint );
    const auto slot = ProposeMint( network_.first, *mint );
    ASSERT_FALSE( slot.empty() );

    // Negative wait discipline: the certificate must NOT appear within a
    // window in which the designated case certifies in seconds.
    EXPECT_FALSE( waitForCondition(
        [&]
        {
            for ( auto *peer : Peers() )
            {
                if ( peer->blockchain->CheckCertificateForSlot( slot ) )
                {
                    return true;
                }
            }
            return false;
        },
        std::chrono::seconds( 10 ) ) )
        << "a dev-chain burn-less mint certified on the test net";
    for ( auto *peer : Peers() )
    {
        EXPECT_FALSE( peer->blockchain->CheckCertificateForSlot( slot ) );
        EXPECT_FALSE( MintedUtxoLanded( *peer, *mint ) ) << "an uncertified mint produced a UTXO";
    }
}

TEST_F( DevMintConsensusTest, RefusesOnNonDesignatedNetEmptyChainIdMintNeverCertifies )
{
    // The actually-reachable hole, closed: a mint-v2 with an EMPTY chain id
    // routes to the public validator and, with no RPC endpoints wired, used to
    // hit the unconditional evidence bypass on every net. On net 963 both
    // bypass legs refuse — the mint is rejected through the scoped bypass.
    StartNetworkOn( version::TEST_NET_ID, "refuses-empty" );

    const auto mint = MintFor( network_.first, "" );
    ASSERT_TRUE( mint );
    const auto slot = ProposeMint( network_.first, *mint );
    ASSERT_FALSE( slot.empty() );

    EXPECT_FALSE( waitForCondition(
        [&]
        {
            for ( auto *peer : Peers() )
            {
                if ( peer->blockchain->CheckCertificateForSlot( slot ) )
                {
                    return true;
                }
            }
            return false;
        },
        std::chrono::seconds( 10 ) ) )
        << "an empty-chain-id burn-less mint certified on the test net";
    for ( auto *peer : Peers() )
    {
        EXPECT_FALSE( peer->blockchain->CheckCertificateForSlot( slot ) );
        EXPECT_FALSE( MintedUtxoLanded( *peer, *mint ) ) << "an uncertified mint produced a UTXO";
    }
}
