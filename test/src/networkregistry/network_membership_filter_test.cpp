/**
 * @file       network_membership_filter_test.cpp
 * @brief      15-11 gap closure (VERIFICATION gaps 1/2/4): proves the
 *             application-layer gossip membership gate. Unit cases pin the
 *             filter semantics (member allow, non-member deny, expired
 *             registry deny, fail-closed from-field helper incl. the
 *             empty-from denial). Flow cases prove the enforcement truths
 *             against real gossip+GlobalDB replication over pnet transports:
 *             an unauthorized SAME-PSK peer cannot participate (its CRDT
 *             writes never enter a member's replicated state) while an
 *             authorized member's writes replicate; runtime membership
 *             widening admits a previously-denied peer's subsequent messages
 *             (per-message consultation); an empty membership set denies
 *             everything (never fails open).
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio.hpp>
#include <boost/dll.hpp>
#include <boost/filesystem.hpp>
#include <gsl/span>
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <libp2p/basic/scheduler/scheduler_impl.hpp>
#include <libp2p/crypto/ed25519_provider/ed25519_provider_impl.hpp>
#include <libp2p/host/host.hpp>
#include <libp2p/multi/multihash.hpp>

#include "account/GeniusAccount.hpp"
#include "base/gossip_auth.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "networkregistry/NetworkMembershipFilter.hpp"
#include "networkregistry/NetworkRegistry.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "securecrdt/securecrdt_test_node.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/wait_condition.hpp"
#include "trustedpeer/TrustedPeerRegistry.hpp"

namespace
{
    using namespace sgns;
    using namespace sgns::networkregistry;
    using sgns::test::assertWaitForCondition;

    constexpr const char *TPR_PRIVATE_KEYS[] = {
        "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eac2",
        "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eac3",
        "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eac4",
    };

    // 0x-hex 32B private-network identity (D-01/D-02 shape from 15-01).
    const std::string kPrivateNetworkId = "0x4e6574776f726b526567697374727954657374496431323334353637383940";

    // Flow-scene network identity (same 0x-hex 32B shape, distinct value).
    const std::string kFlowNetworkId = "0x6d656d6265727368697066696c74657274657374657374313233343536373839";

    // Short hex fingerprint of the (absent) credential -- metadata only (D-03).
    const std::string kPnetKeyFingerprint = "a1b2c3d4e5f60718";

    // Syntactically valid libp2p PeerId strings (network_registry_test
    // kInitialPeers entries) used as the registry membership fixture.
    const std::vector<std::string> kInitialPeers = {
        "12D3KooWJcRGbKZXUDxFUCjkfB2GrEHeMd2QArGhHCKRIAwrP",
        "12D3KooWKhyzo3KdTZ3gHpC1x7zyCuD9QAycBDBskuZtSTPcS",
        "12D3KooWMtQvX7eFtK8khzzgeRfZLdEFh2gRtPYQmXbbFgHiA",
    };

    /// PSK shared by the nodes inside the private network (pubsub_counts
    /// sentinel -- never a production credential).
    constexpr std::string_view SWARM_KEY_PNET = "/key/swarm/psk/1.0.0/\n/base16/"
                                                "000102030405060708090a0b0c0d0e0f101112131415161718"
                                                "191a1b1c1d1e1f\n";

    /// Different PSK -- the "outside" node must fail the pnet handshake.
    constexpr std::string_view SWARM_KEY_OUTSIDE = "/key/swarm/psk/1.0.0/\n/base16/"
                                                   "fffefdfcfbfaf9f8f7f6f5f4f3f2f1f0efeeedecebeae9"
                                                   "e8e7e6e5e4e3e2e1e0\n";

    /// Gossip config matching GossipPubSub's production defaults
    /// (pubsub_counts pattern; sign_messages=true populates `from`).
    libp2p::protocol::gossip::Config MakeGossipConfig()
    {
        libp2p::protocol::gossip::Config config;
        config.echo_forward_mode       = false;
        config.sign_messages           = true;
        config.seen_cache_limit        = 10;
        config.heartbeat_interval_msec = std::chrono::milliseconds{ 100 };
        return config;
    }

    /// Generates a fresh Ed25519 key pair so every node has a distinct identity.
    libp2p::crypto::KeyPair GenerateKeyPair()
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

    /// Derives a fresh, valid PeerId from a freshly generated Ed25519 public
    /// key (sha256 multihash -- the libp2p ed25519 derivation) for use as a
    /// guaranteed non-member identity.
    libp2p::peer::PeerId GenerateFreshPeerId()
    {
        auto keypair = GenerateKeyPair();
        auto multihash_res = libp2p::multi::Multihash::create(
            libp2p::multi::HashType::sha256,
            gsl::span<const uint8_t>( keypair.publicKey.data.data(), keypair.publicKey.data.size() ) );
        auto peer_res = libp2p::peer::PeerId::fromHash( multihash_res.value() );
        return peer_res.value();
    }

    /// @brief Test-owned mutable membership set shared with the broadcaster's
    ///        filter lambda: proves the filter consults membership per
    ///        message (runtime widening admits later messages).
    struct SharedMembership
    {
        std::mutex           mux; ///< guards members
        std::set<std::string> members; ///< libp2p PeerId base58 strings
    };

    /// Builds a broadcaster filter over a shared membership set. An EMPTY set
    /// denies every peer (fail-closed -- the 15-05 posture).
    sgns::networkregistry::MembershipFilter MakeSharedSetFilter( const std::shared_ptr<SharedMembership> &membership )
    {
        return [membership]( const libp2p::peer::PeerId &peer ) {
            std::lock_guard<std::mutex> lock( membership->mux );
            return membership->members.count( peer.toBase58() ) > 0;
        };
    }

    //
    // Unit fixture: real single-node SecureCrdt + 3-peer TPR (the
    // network_registry_test fixture shape); the registry under test holds a
    // single-member membership {memberBase58}.
    //

    class NetworkMembershipFilterTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                { return std::make_shared<MemorySecureStorage>( identifier ); } );
            path_ = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();

            for ( const char *key : TPR_PRIVATE_KEYS )
            {
                auto account = GeniusAccount::NewFromPrivateKey( TokenID::FromBytes( { 0x00 } ), key, path_ );
                tpr_accounts_.push_back( account );
                tpr_peers_.push_back( account->GetAddress() );
            }

            node_ = sgns::test::securecrdt::MakeSecureCrdtTestNode( "networkmembershipfilter" );
            ASSERT_NE( node_, nullptr );

            secure_crdt_ = std::make_shared<sgns::securecrdt::SecureCrdt>( node_->db, "nmf-test" );
            secure_crdt_->RegisterFilters();

            auto tpr_result = sgns::trustedpeer::TrustedPeerRegistry::New(
                secure_crdt_, tpr_peers_, tpr_peers_[0], /*threshold=*/2 );
            ASSERT_FALSE( tpr_result.has_error() ) << tpr_result.error().message();
            tpr_ = tpr_result.value();

            member_base58_ = member_peer_.toBase58();
            ASSERT_FALSE( member_base58_.empty() );

            auto registry_result = NetworkRegistry::New( secure_crdt_,
                                                         tpr_,
                                                         kPrivateNetworkId,
                                                         { member_base58_ },
                                                         /*network_quorum_threshold=*/1,
                                                         {},
                                                         kPnetKeyFingerprint,
                                                         nullptr );
            ASSERT_FALSE( registry_result.has_error() ) << registry_result.error().message();
            registry_ = registry_result.value();
        }

        void TearDown() override
        {
            if ( registry_ )
            {
                registry_->Unregister();
                registry_.reset();
            }
            if ( tpr_ )
            {
                tpr_->Unregister();
                tpr_.reset();
            }
            secure_crdt_.reset();
            node_.reset();
            GeniusAccount::SetSecureStorageFactory( nullptr );
            boost::filesystem::remove_all( path_ );
        }

        boost::filesystem::path                                      path_;
        std::vector<std::shared_ptr<GeniusAccount>>                  tpr_accounts_;
        std::vector<std::string>                                     tpr_peers_;
        std::unique_ptr<sgns::test::securecrdt::SecureCrdtTestNode>  node_;
        std::shared_ptr<sgns::securecrdt::SecureCrdt>                secure_crdt_;
        std::shared_ptr<sgns::trustedpeer::TrustedPeerRegistry>      tpr_;
        std::shared_ptr<NetworkRegistry>                             registry_;
        std::string                                                  member_base58_;
        /// PeerId has no default constructor: derive the member identity
        /// from a freshly generated Ed25519 key (a fully valid PeerId; the
        /// kInitialPeers string literals are opaque to PeerId::fromBase58).
        libp2p::peer::PeerId                                         member_peer_{ GenerateFreshPeerId() };
    };

    // (1) A member peer passes the registry-backed filter.
    TEST_F( NetworkMembershipFilterTest, MemberPeerIsAuthorized )
    {
        auto filter = MakeNetworkMembershipFilter( registry_ );
        ASSERT_TRUE( filter );
        EXPECT_TRUE( filter( member_peer_ ) );
    }

    // (2) A freshly generated (non-member) peer is denied.
    TEST_F( NetworkMembershipFilterTest, NonMemberPeerIsDenied )
    {
        auto filter = MakeNetworkMembershipFilter( registry_ );
        const auto outsider = GenerateFreshPeerId();
        EXPECT_NE( outsider.toBase58(), member_base58_ );
        EXPECT_FALSE( filter( outsider ) );
    }

    // (3) An expired registry weak_ptr fail-closes: every peer is denied
    //     (the filter never fails open when the registry is gone).
    //
    //     Deviation note: a New-constructed registry cannot be used for this
    //     case -- SecureCrdt::RegisterFilters' element-filter lambda captures
    //     the D-04 registry entry (whose peer_registry is a STRONG shared_ptr,
    //     the documented phase-13 shared-ownership precedent) by value, so
    //     Unregister()+reset leaves the registry pinned (and functional)
    //     until the datastore dies. An UNREGISTERED registry built through
    //     the public constructor is therefore the shape whose destruction
    //     genuinely expires the weak_ptr while secure_crdt_/node_ stay alive.
    TEST_F( NetworkMembershipFilterTest, ExpiredRegistryDeniesAll )
    {
        auto live_filter = MakeNetworkMembershipFilter( registry_ );
        EXPECT_TRUE( live_filter( member_peer_ ) ); // sanity: New-path allows members

        sgns::networkregistry::MembershipFilter filter;
        {
            auto temp = std::make_shared<NetworkRegistry>( secure_crdt_,
                                                           tpr_,
                                                           kPrivateNetworkId,
                                                           std::vector<std::string>{ member_base58_ },
                                                           /*network_quorum_threshold=*/1,
                                                           std::vector<std::string>{},
                                                           kPnetKeyFingerprint,
                                                           NetworkRegistry::DefaultBaseKey( kPrivateNetworkId ),
                                                           nullptr );
            filter = MakeNetworkMembershipFilter( temp );
            ASSERT_TRUE( filter );
            EXPECT_TRUE( filter( member_peer_ ) ) << "sanity: authorized while registry live";
        } // temp destroyed: nothing pins an unregistered registry

        ASSERT_TRUE( filter ); // the installed filter itself outlives the registry
        EXPECT_FALSE( filter( member_peer_ ) ) << "expired registry must deny every peer";
        EXPECT_FALSE( filter( GenerateFreshPeerId() ) );
    }

    // (4) AuthorizeGossipSender helper decision table, including the
    //     unconditional from-field emptiness denial (empty ByteArray fails
    //     PeerId::fromBytes -> DENIED, never skipped).
    TEST_F( NetworkMembershipFilterTest, AuthorizeGossipSenderHelper )
    {
        const sgns::networkregistry::MembershipFilter no_filter;
        const auto filter = MakeNetworkMembershipFilter( registry_ );

        const libp2p::common::ByteArray member_from_bytes( member_peer_.toVector() );
        const libp2p::common::ByteArray outsider_from_bytes( GenerateFreshPeerId().toVector() );
        const libp2p::common::ByteArray garbage_from_bytes = { 0x01, 0x02, 0x03 };
        const libp2p::common::ByteArray empty_from_bytes   = {};

        // No filter installed -> public pass-through for ANY sender bytes.
        EXPECT_TRUE( sgns::networkregistry::AuthorizeGossipSender( no_filter, member_from_bytes ) );
        EXPECT_TRUE( sgns::networkregistry::AuthorizeGossipSender( no_filter, garbage_from_bytes ) );
        EXPECT_TRUE( sgns::networkregistry::AuthorizeGossipSender( no_filter, empty_from_bytes ) );

        // Filter installed: member allowed, non-member / garbage / EMPTY denied.
        EXPECT_TRUE( sgns::networkregistry::AuthorizeGossipSender( filter, member_from_bytes ) );
        EXPECT_FALSE( sgns::networkregistry::AuthorizeGossipSender( filter, outsider_from_bytes ) );
        EXPECT_FALSE( sgns::networkregistry::AuthorizeGossipSender( filter, garbage_from_bytes ) );
        EXPECT_FALSE( sgns::networkregistry::AuthorizeGossipSender( filter, empty_from_bytes ) );
    }

    // (4b) CR-G01 decision table for the application-layer payload
    //      authenticator (the forged-from proof at the primitive level): a
    //      same-PSK attacker sealing with its OWN key cannot pass a member's
    //      from-field; tampered payloads and corrupted signatures fail
    //      verification; raw data is recognized as not-an-envelope; an empty
    //      from is denied under an otherwise-valid seal (fail-closed).
    TEST( GossipPayloadAuthDecisionTable, SealOpenForgeAndTamperCases )
    {
        using sgns::base::GossipPayloadAuthError;

        const libp2p::crypto::KeyPair member_key  = GenerateKeyPair();
        const libp2p::crypto::KeyPair attacker_key = GenerateKeyPair();

        auto member_from_res  = sgns::base::DeriveGossipFromBytes( member_key );
        auto attacker_from_res = sgns::base::DeriveGossipFromBytes( attacker_key );
        ASSERT_FALSE( member_from_res.has_error() );
        ASSERT_FALSE( attacker_from_res.has_error() );
        const auto member_from  = member_from_res.value();
        const auto attacker_from = attacker_from_res.value();
        ASSERT_NE( member_from, attacker_from ) << "member and attacker derived the same PeerId";

        const std::vector<uint8_t> payload = { 0x01, 0x02, 0x03, 0x04, 0x05 };

        // (a) Honest seal -> open round trip: payload intact, authenticated
        //     peer equals the PeerId derived from the sealing key.
        auto sealed = sgns::base::SealGossipPayload( member_key, member_from, payload );
        ASSERT_FALSE( sealed.has_error() ) << "honest sealing failed";
        auto opened = sgns::base::OpenGossipPayload( member_from, sealed.value() );
        ASSERT_FALSE( opened.has_error() ) << "honest envelope failed to open: "
                                           << static_cast<int>( opened.error() );
        const std::vector<uint8_t> inner_payload( opened.value().payload.begin(),
                                                  opened.value().payload.end() );
        EXPECT_EQ( inner_payload, payload );
        auto member_peer_id = libp2p::peer::PeerId::fromBytes( member_from );
        ASSERT_FALSE( member_peer_id.has_error() );
        EXPECT_EQ( opened.value().authenticated_peer, member_peer_id.value() );

        // (b) FORGED FROM (the CR-G01 core): envelope sealed by the attacker's
        //     key but presented under the member's from-field. The binding
        //     check denies it: the embedded key derives the ATTACKER's PeerId,
        //     not the member's.
        auto forged = sgns::base::SealGossipPayload( attacker_key, member_from, payload );
        ASSERT_FALSE( forged.has_error() );
        auto opened_forged = sgns::base::OpenGossipPayload( member_from, forged.value() );
        ASSERT_TRUE( opened_forged.has_error() );
        EXPECT_EQ( opened_forged.error(), GossipPayloadAuthError::KEY_FROM_MISMATCH );

        // (c) Tampered payload: one flipped byte AFTER sealing.
        auto tampered     = sealed.value();
        tampered.back()   = tampered.back() ^ 0xFF;
        auto opened_tampered = sgns::base::OpenGossipPayload( member_from, tampered );
        ASSERT_TRUE( opened_tampered.has_error() );
        EXPECT_EQ( opened_tampered.error(), GossipPayloadAuthError::SIGNATURE_INVALID );

        // (d) Corrupted signature bytes: the signature region starts after
        //     magic + u32be key length + key + u32be; flip its first byte.
        const auto &envelope = sealed.value();
        ASSERT_GT( envelope.size(), sgns::base::kGossipAuthEnvelopeMagic.size() + 8 );
        const size_t key_len_offset = sgns::base::kGossipAuthEnvelopeMagic.size();
        const uint32_t key_len = ( static_cast<uint32_t>( envelope[key_len_offset] ) << 24 )
                               | ( static_cast<uint32_t>( envelope[key_len_offset + 1] ) << 16 )
                               | ( static_cast<uint32_t>( envelope[key_len_offset + 2] ) << 8 )
                               | static_cast<uint32_t>( envelope[key_len_offset + 3] );
        const size_t signature_offset = key_len_offset + 4 + key_len + 4;
        ASSERT_LT( signature_offset, envelope.size() );
        auto corrupted   = envelope;
        corrupted[signature_offset] = corrupted[signature_offset] ^ 0xFF;
        auto opened_corrupted = sgns::base::OpenGossipPayload( member_from, corrupted );
        ASSERT_TRUE( opened_corrupted.has_error() );
        EXPECT_EQ( opened_corrupted.error(), GossipPayloadAuthError::SIGNATURE_INVALID );

        // (e) Raw payload with no envelope magic: a distinct not-an-envelope
        //     error (a set filter treats this as deny -- fail-closed).
        auto opened_raw = sgns::base::OpenGossipPayload( member_from, payload );
        ASSERT_TRUE( opened_raw.has_error() );
        EXPECT_EQ( opened_raw.error(), GossipPayloadAuthError::NOT_AN_ENVELOPE );

        // (f) EMPTY from under an honest seal: PeerId::fromBytes fails inside
        //     the binding check -> deny (no emptiness skip branch).
        auto opened_empty_from = sgns::base::OpenGossipPayload( {}, sealed.value() );
        ASSERT_TRUE( opened_empty_from.has_error() );
        EXPECT_EQ( opened_empty_from.error(), GossipPayloadAuthError::KEY_FROM_MISMATCH );
    }

    //
    // Flow fixture: pnet pubsub nodes carrying real GlobalDBs
    // (pubsub_graphsync replication shape + pubsub_counts pnet nodes).
    //

    /// One pnet gossip node with its own GlobalDB (graphsync over the node's
    /// host, per pubsub_graphsync_test's GlobalDB::New wiring).
    struct PnetGdbNode
    {
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub>                     pubsub;
        std::shared_ptr<const libp2p::crypto::KeyPair>                       signing_key;
        std::shared_ptr<libp2p::basic::Scheduler>                            scheduler;
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::Network>           graphsync_network;
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator;
        std::shared_ptr<sgns::crdt::GlobalDB>                                db;
    };

    class NetworkMembershipFilterFlowTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // Injector libp2p components segfault without a logging system
            // (15-08 Deviation 4); the once-flag helper installs soralog.
            sgns::test::securecrdt::EnsureLoggingSystemConfigured();
        }

        /// @brief Builds one pnet GlobalDB node with a unique on-disk path
        ///        next to the test binary (MakeSecureCrdtTestNode layout).
        std::shared_ptr<PnetGdbNode> MakeNode( const std::shared_ptr<boost::asio::io_context> &io,
                                               const std::string                             &name,
                                               libp2p::crypto::KeyPair                        keypair,
                                               const std::string                             &swarm_key )
        {
            const std::string testName   = ::testing::UnitTest::GetInstance()->current_test_info()->name();
            const std::string binaryPath = boost::dll::program_location().parent_path().string();
            const std::string basePath   = binaryPath + "/" + name + "_" + testName;
            sgns::test::removeAllWithRetry( basePath );

            auto node   = std::make_shared<PnetGdbNode>();
            // CR-G01 fixture repair: retain a copy of the gossip host keypair
            // BEFORE it is moved into GossipPubSub -- a gated node seals its
            // private-network publishes with exactly this key (it derives the
            // transport from-field the vendored gossip stamps).
            node->signing_key = std::make_shared<const libp2p::crypto::KeyPair>( keypair );
            node->pubsub = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>( std::move( keypair ),
                                                                              MakeGossipConfig(),
                                                                              swarm_key );
            if ( node->pubsub->Start( 0, {} ).get() )
            {
                return nullptr;
            }

            node->scheduler = std::make_shared<libp2p::basic::SchedulerImpl>(
                std::make_shared<libp2p::basic::AsioSchedulerBackend>( io ),
                libp2p::basic::Scheduler::Config{ std::chrono::milliseconds( 100 ) } );
            node->graphsync_network = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>(
                node->pubsub->GetHost(), node->scheduler );
            node->generator = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();

            auto db_result = sgns::crdt::GlobalDB::New( io,
                                                        basePath,
                                                        node->pubsub,
                                                        sgns::crdt::CrdtOptions::DefaultOptions(),
                                                        node->graphsync_network,
                                                        node->scheduler,
                                                        node->generator );
            if ( db_result.has_error() )
            {
                return nullptr;
            }
            node->db = std::move( db_result.value() );
            return node;
        }

        /// Subscribes a node's GlobalDB to the scoped topic (both directions).
        void JoinTopic( PnetGdbNode &node, const std::string &topic )
        {
            node.db->AddBroadcastTopic( topic );
            node.db->AddListenTopic( topic );
            node.db->Start();
        }

        /// Commits one CRDT key/value write on `db` (pubsub_graphsync shape).
        void CommitPut( sgns::crdt::GlobalDB                          &db,
                        const sgns::crdt::HierarchicalKey             &key,
                        const std::vector<uint8_t>                    &value,
                        const std::string                             &topic )
        {
            auto                       transaction = db.BeginTransaction();
            sgns::crdt::GlobalDB::Buffer data;
            data.put( gsl::span<const uint8_t>( value ) );
            ASSERT_FALSE( transaction->Put( key, data ).has_error() );
            ASSERT_FALSE( transaction->Commit( { topic } ).has_error() );
        }

        bool HasKey( sgns::crdt::GlobalDB &db, const sgns::crdt::HierarchicalKey &key )
        {
            return db.Get( key ).has_value();
        }

        /// @brief Bounded negative window (pubsub_counts grace-loop pattern):
        ///        the key must stay ABSENT for the whole window -- ASSERT on
        ///        every Get attempt, then a final EXPECT.
        void AssertKeyNeverPresentWithin( sgns::crdt::GlobalDB              &db,
                                          const sgns::crdt::HierarchicalKey &key,
                                          std::chrono::milliseconds          window,
                                          const std::string                 &what )
        {
            const auto deadline = std::chrono::steady_clock::now() + window;
            while ( std::chrono::steady_clock::now() < deadline )
            {
                ASSERT_FALSE( HasKey( db, key ) ) << what << " replicated although it must be denied";
                std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
            }
            EXPECT_FALSE( HasKey( db, key ) ) << what << " present at end of negative window";
        }

        bool IsConnectedTo( const std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> &pubs,
                            const libp2p::peer::PeerId                             &id )
        {
            libp2p::peer::PeerInfo info{ id, {} };
            return pubs->GetHost()->connectedness( info ) == libp2p::Host::Connectedness::CONNECTED;
        }

        /// Wired teardown: clear the filter, stop io, join, stop pubsubs.
        void TearDownNodes( const std::shared_ptr<sgns::crdt::PubSubBroadcasterExt> &broadcaster,
                            const std::shared_ptr<boost::asio::io_context>         &io_context,
                            std::thread                                           &io_thread,
                            std::initializer_list<std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub>> pubsubs )
        {
            if ( broadcaster )
            {
                broadcaster->ClearMembershipFilter();
            }
            io_context->stop();
            if ( io_thread.joinable() )
            {
                io_thread.join();
            }
            for ( auto &pubs : pubsubs )
            {
                if ( pubs )
                {
                    pubs->Stop();
                }
            }
        }
    };

    // (5) Gap-1/gap-4 truth: a same-PSK peer outside the membership meshes at
    //     the transport layer but cannot PARTICIPATE -- its CRDT writes never
    //     enter ANY gated member's replicated state -- while an authorized
    //     member's writes replicate member-to-member; a wrong-PSK node never
    //     even connects. Both members install the gate (an ungated member
    //     would merge and re-origin the intruder's delta via its head
    //     rebroadcasts, muling the data past the receiver's filter).
    TEST_F( NetworkMembershipFilterFlowTest, UnauthorizedSamePskPeerCannotParticipateWhileMembersCan )
    {
        const std::string                topic             = std::string( "chain/" ) + kFlowNetworkId + "/tasks";
        const sgns::crdt::HierarchicalKey key_from_b( "/chain/" + kFlowNetworkId + "/fromB" );
        const sgns::crdt::HierarchicalKey key_from_intruder( "/chain/" + kFlowNetworkId + "/fromIntruder" );

        auto membership = std::make_shared<SharedMembership>();
        auto io_context = std::make_shared<boost::asio::io_context>();

        auto pnetA     = MakeNode( io_context, "nmf_flow5_A", GenerateKeyPair(), std::string( SWARM_KEY_PNET ) );
        auto pnetB     = MakeNode( io_context, "nmf_flow5_B", GenerateKeyPair(), std::string( SWARM_KEY_PNET ) );
        auto intruder  = MakeNode( io_context, "nmf_flow5_I", GenerateKeyPair(), std::string( SWARM_KEY_PNET ) );
        ASSERT_NE( pnetA, nullptr );
        ASSERT_NE( pnetB, nullptr );
        ASSERT_NE( intruder, nullptr );

        auto publicControl = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(
            GenerateKeyPair(), MakeGossipConfig(), std::string( SWARM_KEY_OUTSIDE ) );
        ASSERT_FALSE( publicControl->Start( 0, {} ).get() ) << "public control failed to start";

        const auto idA        = pnetA->pubsub->GetHost()->getId();
        const auto idB        = pnetB->pubsub->GetHost()->getId();
        const auto idIntruder = intruder->pubsub->GetHost()->getId();
        const auto idPublic   = publicControl->GetHost()->getId();
        ASSERT_EQ( ( std::set<libp2p::peer::PeerId>{ idA, idB, idIntruder, idPublic } ).size(), 4u )
            << "flow nodes silently share a host";

        JoinTopic( *pnetA, topic );
        JoinTopic( *pnetB, topic );
        JoinTopic( *intruder, topic );

        // Membership gate on EVERY member node (only pnetB is a member).
        // Both gated members are load-bearing for the negative window: an
        // UNGATED member would merge the intruder's delta and re-origin it --
        // CrdtDatastore::RebroadcastHeads republishes the per-topic head list
        // (now containing the intruder's head CID) under the member's OWN
        // identity, and CIDs carry no origin, so the filtered node would
        // authorize the announcement and graphsync-fetch the intruder's
        // blocks from its trusted member. Gating only the receiver made the
        // negative window a race against that relay (intermittent failures);
        // with every member gated there is no path left for the intruder's
        // data to enter ANY member's replicated state.
        membership->members.insert( idB.toBase58() );
        auto broadcaster_a = pnetA->db->GetBroadcaster();
        ASSERT_NE( broadcaster_a, nullptr );
        broadcaster_a->SetMembershipFilter( MakeSharedSetFilter( membership ) );
        broadcaster_a->SetGossipSigningKey( pnetA->signing_key );
        EXPECT_TRUE( broadcaster_a->HasMembershipFilter() );
        EXPECT_TRUE( broadcaster_a->HasGossipSigningKey() );
        auto broadcaster_b = pnetB->db->GetBroadcaster();
        ASSERT_NE( broadcaster_b, nullptr );
        broadcaster_b->SetMembershipFilter( MakeSharedSetFilter( membership ) );
        broadcaster_b->SetGossipSigningKey( pnetB->signing_key );
        EXPECT_TRUE( broadcaster_b->HasMembershipFilter() );
        EXPECT_TRUE( broadcaster_b->HasGossipSigningKey() );

        // CR-G01: the intruder is gated and keyed TOO (every private node
        // gates; every gated node seals), so its denial stays attributable to
        // the MEMBERSHIP layer -- its envelope authenticates fine (own key,
        // own from), it simply is not a member. An ungated raw publisher
        // would be denied one layer earlier (UnsignedPayloadFromMember scene).
        auto broadcaster_i = intruder->db->GetBroadcaster();
        ASSERT_NE( broadcaster_i, nullptr );
        broadcaster_i->SetMembershipFilter( MakeSharedSetFilter( membership ) );
        broadcaster_i->SetGossipSigningKey( intruder->signing_key );
        EXPECT_TRUE( broadcaster_i->HasMembershipFilter() );

        // All three same-PSK pairs mesh; the public control dials and must
        // fail the pnet handshake.
        pnetA->pubsub->AddPeers( { pnetB->pubsub->GetInterfaceAddress() } );
        pnetB->pubsub->AddPeers( { pnetA->pubsub->GetInterfaceAddress() } );
        pnetA->pubsub->AddPeers( { intruder->pubsub->GetInterfaceAddress() } );
        intruder->pubsub->AddPeers( { pnetA->pubsub->GetInterfaceAddress() } );
        pnetB->pubsub->AddPeers( { intruder->pubsub->GetInterfaceAddress() } );
        intruder->pubsub->AddPeers( { pnetB->pubsub->GetInterfaceAddress() } );
        pnetA->pubsub->AddPeers( { publicControl->GetInterfaceAddress() } );

        std::thread io_thread( [io_context]() { io_context->run(); } );

        // Transport precondition: BOTH same-PSK writers are connected to the
        // receiver, so the intruder's denial happens at the message level,
        // not because it never reached the node.
        ASSERT_WAIT_FOR_CONDITION(
            [&]()
            {
                return IsConnectedTo( pnetA->pubsub, idB ) && IsConnectedTo( pnetA->pubsub, idIntruder );
            },
            std::chrono::milliseconds( 15000 ),
            "same-PSK peers did not connect to the filtered node",
            nullptr );

        // (a) The authorized member participates: its write replicates in.
        CommitPut( *pnetB->db, key_from_b, { 0xde, 0xad, 0xbe, 0xef }, topic );
        assertWaitForCondition(
            [&]()
            {
                auto result = pnetA->db->Get( key_from_b );
                return result.has_value();
            },
            std::chrono::milliseconds( 20000 ),
            "authorized member write did not replicate to the filtered node" );

        // (b) The same-PSK non-member cannot participate: its write is dropped
        //     at ingest by BOTH gated members (the 100ms rebroadcast loop
        //     keeps retrying, so the windows observe repeated denials, not a
        //     one-shot miss). Checking pnetB too pins the second gate --
        //     an ungated member would relay the intruder's delta onward (see
        //     the filter-install comment above).
        CommitPut( *intruder->db, key_from_intruder, { 0x13, 0x37, 0x13, 0x37 }, topic );
        AssertKeyNeverPresentWithin( *pnetA->db,
                                     key_from_intruder,
                                     std::chrono::milliseconds( 3000 ),
                                     "intruder write" );
        AssertKeyNeverPresentWithin( *pnetB->db,
                                     key_from_intruder,
                                     std::chrono::milliseconds( 3000 ),
                                     "intruder write (gated member)" );

        // (c) Transport-boundary control: the wrong-PSK node never connects.
        EXPECT_FALSE( IsConnectedTo( pnetA->pubsub, idPublic ) )
            << "public control node connected despite pnet mismatch";

        broadcaster_b->ClearMembershipFilter();
        broadcaster_i->ClearMembershipFilter();
        TearDownNodes( broadcaster_a, io_context, io_thread,
                       { pnetA->pubsub, pnetB->pubsub, intruder->pubsub, publicControl } );
    }

    // (6) The 15-05 runtime-admission truth: one live filter instance denies
    //     a peer's messages while it is outside the set and admits that same
    //     peer's subsequent messages after the set widens -- per-message
    //     consultation, not an install-time snapshot.
    TEST_F( NetworkMembershipFilterFlowTest, MembershipWideningAdmitsNewPeerAtRuntime )
    {
        const std::string                topic             = std::string( "chain/" ) + kFlowNetworkId + "/tasks";
        const sgns::crdt::HierarchicalKey key_first( "/chain/" + kFlowNetworkId + "/fromB_first" );
        const sgns::crdt::HierarchicalKey key_second( "/chain/" + kFlowNetworkId + "/fromB_second" );

        auto membership = std::make_shared<SharedMembership>();
        auto io_context = std::make_shared<boost::asio::io_context>();

        auto pnetA = MakeNode( io_context, "nmf_flow6_A", GenerateKeyPair(), std::string( SWARM_KEY_PNET ) );
        auto pnetB = MakeNode( io_context, "nmf_flow6_B", GenerateKeyPair(), std::string( SWARM_KEY_PNET ) );
        ASSERT_NE( pnetA, nullptr );
        ASSERT_NE( pnetB, nullptr );

        const auto idA = pnetA->pubsub->GetHost()->getId();
        const auto idB = pnetB->pubsub->GetHost()->getId();
        ASSERT_NE( idA, idB );

        JoinTopic( *pnetA, topic );
        JoinTopic( *pnetB, topic );

        // Membership initially EXCLUDES pnetB (holds an unrelated id).
        membership->members.insert( kInitialPeers[2] );
        auto broadcaster_a = pnetA->db->GetBroadcaster();
        ASSERT_NE( broadcaster_a, nullptr );
        broadcaster_a->SetMembershipFilter( MakeSharedSetFilter( membership ) );
        broadcaster_a->SetGossipSigningKey( pnetA->signing_key );
        // CR-G01 fixture repair: pnetB must SEAL its publishes (the gated
        // receiver denies raw data), so it is gated+keyed with the same
        // shared-set filter -- its denial then stays attributable to the
        // membership layer, and after the widening its sealed writes land.
        auto broadcaster_b = pnetB->db->GetBroadcaster();
        ASSERT_NE( broadcaster_b, nullptr );
        broadcaster_b->SetMembershipFilter( MakeSharedSetFilter( membership ) );
        broadcaster_b->SetGossipSigningKey( pnetB->signing_key );

        pnetA->pubsub->AddPeers( { pnetB->pubsub->GetInterfaceAddress() } );
        pnetB->pubsub->AddPeers( { pnetA->pubsub->GetInterfaceAddress() } );

        std::thread io_thread( [io_context]() { io_context->run(); } );

        ASSERT_WAIT_FOR_CONDITION(
            [&]() { return IsConnectedTo( pnetA->pubsub, idB ); },
            std::chrono::milliseconds( 15000 ),
            "pnetB did not connect to the filtered node",
            nullptr );

        // First write while EXCLUDED: never lands (continuous rebroadcasts
        // are all denied throughout the window).
        CommitPut( *pnetB->db, key_first, { 0xde, 0xad, 0xbe, 0xef }, topic );
        AssertKeyNeverPresentWithin( *pnetA->db,
                                     key_first,
                                     std::chrono::milliseconds( 4000 ),
                                     "excluded peer's first write" );

        // Widen the shared set -- no filter reinstall, same live instance.
        {
            std::lock_guard<std::mutex> lock( membership->mux );
            membership->members.insert( idB.toBase58() );
        }

        // Second write by the same peer now replicates in.
        CommitPut( *pnetB->db, key_second, { 0xbe, 0xef, 0xbe, 0xef }, topic );
        assertWaitForCondition(
            [&]()
            {
                auto result = pnetA->db->Get( key_second );
                return result.has_value();
            },
            std::chrono::milliseconds( 20000 ),
            "newly admitted peer's second write did not replicate" );

        broadcaster_b->ClearMembershipFilter();
        TearDownNodes( broadcaster_a, io_context, io_thread, { pnetA->pubsub, pnetB->pubsub } );
    }

    // (7) Fail-closed at flow level: an EMPTY membership set denies a fully
    //     connected same-PSK member -- the filter never fails open.
    TEST_F( NetworkMembershipFilterFlowTest, EmptyMembershipDeniesEverything )
    {
        const std::string                topic        = std::string( "chain/" ) + kFlowNetworkId + "/tasks";
        const sgns::crdt::HierarchicalKey key_from_b( "/chain/" + kFlowNetworkId + "/fromB" );

        auto membership = std::make_shared<SharedMembership>(); // stays EMPTY
        auto io_context = std::make_shared<boost::asio::io_context>();

        auto pnetA = MakeNode( io_context, "nmf_flow7_A", GenerateKeyPair(), std::string( SWARM_KEY_PNET ) );
        auto pnetB = MakeNode( io_context, "nmf_flow7_B", GenerateKeyPair(), std::string( SWARM_KEY_PNET ) );
        ASSERT_NE( pnetA, nullptr );
        ASSERT_NE( pnetB, nullptr );

        const auto idA = pnetA->pubsub->GetHost()->getId();
        const auto idB = pnetB->pubsub->GetHost()->getId();
        ASSERT_NE( idA, idB );

        JoinTopic( *pnetA, topic );
        JoinTopic( *pnetB, topic );

        auto broadcaster_a = pnetA->db->GetBroadcaster();
        ASSERT_NE( broadcaster_a, nullptr );
        broadcaster_a->SetMembershipFilter( MakeSharedSetFilter( membership ) );
        broadcaster_a->SetGossipSigningKey( pnetA->signing_key );
        EXPECT_TRUE( broadcaster_a->HasMembershipFilter() );
        // CR-G01 fixture repair: gate+key the writer too so it SEALS (the
        // gated receiver denies raw data); the denial then remains
        // attributable to the EMPTY membership set, not to a missing envelope.
        auto broadcaster_b = pnetB->db->GetBroadcaster();
        ASSERT_NE( broadcaster_b, nullptr );
        broadcaster_b->SetMembershipFilter( MakeSharedSetFilter( membership ) );
        broadcaster_b->SetGossipSigningKey( pnetB->signing_key );

        pnetA->pubsub->AddPeers( { pnetB->pubsub->GetInterfaceAddress() } );
        pnetB->pubsub->AddPeers( { pnetA->pubsub->GetInterfaceAddress() } );

        std::thread io_thread( [io_context]() { io_context->run(); } );

        ASSERT_WAIT_FOR_CONDITION(
            [&]() { return IsConnectedTo( pnetA->pubsub, idB ); },
            std::chrono::milliseconds( 15000 ),
            "same-PSK member did not connect to the filtered node",
            nullptr );

        // Fully connected, same PSK -- but empty membership denies the write.
        CommitPut( *pnetB->db, key_from_b, { 0xde, 0xad, 0xbe, 0xef }, topic );
        AssertKeyNeverPresentWithin( *pnetA->db,
                                     key_from_b,
                                     std::chrono::milliseconds( 4000 ),
                                     "member write under empty membership" );

        broadcaster_b->ClearMembershipFilter();
        TearDownNodes( broadcaster_a, io_context, io_thread, { pnetA->pubsub, pnetB->pubsub } );
    }

    // (8) CR-G01 forged-from proof at the flow level: a MEMBER (membership
    //     includes it; honest transport from = its own host id) whose envelope
    //     is sealed under ANOTHER member's public key is DROPPED by the gated
    //     ingest -- only the authentication check can deny here, because
    //     membership alone would admit it (non-vacuity by construction). The
    //     honestly-sealed member keeps replicating (positive control).
    //
    //     The impostor shape: the broadcaster's signing key is wired to the
    //     OTHER member's keypair, so every envelope it publishes embeds a key
    //     deriving the other member's PeerId while the transport from-field
    //     carries the impostor's own id -> OpenGossipPayload KEY_FROM_MISMATCH
    //     -> deny before any membership consultation. (A garbage-signature
    //     variant of the same envelope is pinned at the unit level, case (d).)
    TEST_F( NetworkMembershipFilterFlowTest, MemberImpostorEnvelopeIsDroppedByGatedIngest )
    {
        const std::string                topic = std::string( "chain/" ) + kFlowNetworkId + "/tasks";
        const sgns::crdt::HierarchicalKey key_honest( "/chain/" + kFlowNetworkId + "/fromHonest" );
        const sgns::crdt::HierarchicalKey key_impostor( "/chain/" + kFlowNetworkId + "/fromImpostor" );

        auto membership = std::make_shared<SharedMembership>();
        auto io_context = std::make_shared<boost::asio::io_context>();

        auto gated    = MakeNode( io_context, "nmf_flow8_A", GenerateKeyPair(), std::string( SWARM_KEY_PNET ) );
        auto honest   = MakeNode( io_context, "nmf_flow8_H", GenerateKeyPair(), std::string( SWARM_KEY_PNET ) );
        auto impostor = MakeNode( io_context, "nmf_flow8_I", GenerateKeyPair(), std::string( SWARM_KEY_PNET ) );
        ASSERT_NE( gated, nullptr );
        ASSERT_NE( honest, nullptr );
        ASSERT_NE( impostor, nullptr );

        const auto idGated    = gated->pubsub->GetHost()->getId();
        const auto idHonest   = honest->pubsub->GetHost()->getId();
        const auto idImpostor = impostor->pubsub->GetHost()->getId();
        ASSERT_EQ( ( std::set<libp2p::peer::PeerId>{ idGated, idHonest, idImpostor } ).size(), 3u );

        JoinTopic( *gated, topic );
        JoinTopic( *honest, topic );
        JoinTopic( *impostor, topic );

        // BOTH writers are members: membership alone would admit the impostor.
        membership->members.insert( idHonest.toBase58() );
        membership->members.insert( idImpostor.toBase58() );

        auto broadcaster_gated = gated->db->GetBroadcaster();
        ASSERT_NE( broadcaster_gated, nullptr );
        broadcaster_gated->SetMembershipFilter( MakeSharedSetFilter( membership ) );
        broadcaster_gated->SetGossipSigningKey( gated->signing_key );

        auto broadcaster_honest = honest->db->GetBroadcaster();
        ASSERT_NE( broadcaster_honest, nullptr );
        broadcaster_honest->SetMembershipFilter( MakeSharedSetFilter( membership ) );
        broadcaster_honest->SetGossipSigningKey( honest->signing_key );

        // The IMPOSTOR: gated like every private node, but its sealing key is
        // the OTHER member's keypair -- its envelopes claim honest's identity.
        auto broadcaster_impostor = impostor->db->GetBroadcaster();
        ASSERT_NE( broadcaster_impostor, nullptr );
        broadcaster_impostor->SetMembershipFilter( MakeSharedSetFilter( membership ) );
        broadcaster_impostor->SetGossipSigningKey( honest->signing_key );
        EXPECT_TRUE( broadcaster_impostor->HasGossipSigningKey() );

        gated->pubsub->AddPeers( { honest->pubsub->GetInterfaceAddress() } );
        honest->pubsub->AddPeers( { gated->pubsub->GetInterfaceAddress() } );
        gated->pubsub->AddPeers( { impostor->pubsub->GetInterfaceAddress() } );
        impostor->pubsub->AddPeers( { gated->pubsub->GetInterfaceAddress() } );

        std::thread io_thread( [io_context]() { io_context->run(); } );

        ASSERT_WAIT_FOR_CONDITION(
            [&]()
            {
                return IsConnectedTo( gated->pubsub, idHonest ) && IsConnectedTo( gated->pubsub, idImpostor );
            },
            std::chrono::milliseconds( 15000 ),
            "members did not connect to the gated node",
            nullptr );

        // (a) Positive control: the honestly-sealed member's write replicates.
        CommitPut( *honest->db, key_honest, { 0xde, 0xad, 0xbe, 0xef }, topic );
        assertWaitForCondition(
            [&]()
            {
                auto result = gated->db->Get( key_honest );
                return result.has_value();
            },
            std::chrono::milliseconds( 20000 ),
            "honestly sealed member write did not replicate to the gated node" );

        // (b) Impostor write: membership would admit it (it IS a member), but
        //     its envelope claims another member's key -> dropped at ingest;
        //     its data never enters the gated node's replicated state.
        CommitPut( *impostor->db, key_impostor, { 0x13, 0x37, 0x13, 0x37 }, topic );
        AssertKeyNeverPresentWithin( *gated->db,
                                     key_impostor,
                                     std::chrono::milliseconds( 3000 ),
                                     "impostor envelope write" );

        broadcaster_honest->ClearMembershipFilter();
        broadcaster_impostor->ClearMembershipFilter();
        TearDownNodes( broadcaster_gated, io_context, io_thread,
                       { gated->pubsub, honest->pubsub, impostor->pubsub } );
    }

    // (9) CR-G01 fail-closed proof: a MEMBER publishing RAW (unsealed) data is
    //     dropped by the gated ingest -- under a set filter the absence of an
    //     envelope is itself a denial, even though membership would admit the
    //     sender. The sealed positive control keeps flowing.
    TEST_F( NetworkMembershipFilterFlowTest, UnsignedPayloadFromMemberIsDroppedUnderFilter )
    {
        const std::string                topic = std::string( "chain/" ) + kFlowNetworkId + "/tasks";
        const sgns::crdt::HierarchicalKey key_sealed( "/chain/" + kFlowNetworkId + "/fromSealed" );
        const sgns::crdt::HierarchicalKey key_raw( "/chain/" + kFlowNetworkId + "/fromRaw" );

        auto membership = std::make_shared<SharedMembership>();
        auto io_context = std::make_shared<boost::asio::io_context>();

        auto gated  = MakeNode( io_context, "nmf_flow9_A", GenerateKeyPair(), std::string( SWARM_KEY_PNET ) );
        auto sealer = MakeNode( io_context, "nmf_flow9_S", GenerateKeyPair(), std::string( SWARM_KEY_PNET ) );
        auto rawp   = MakeNode( io_context, "nmf_flow9_R", GenerateKeyPair(), std::string( SWARM_KEY_PNET ) );
        ASSERT_NE( gated, nullptr );
        ASSERT_NE( sealer, nullptr );
        ASSERT_NE( rawp, nullptr );

        const auto idGated  = gated->pubsub->GetHost()->getId();
        const auto idSealer = sealer->pubsub->GetHost()->getId();
        const auto idRaw    = rawp->pubsub->GetHost()->getId();
        ASSERT_EQ( ( std::set<libp2p::peer::PeerId>{ idGated, idSealer, idRaw } ).size(), 3u );

        JoinTopic( *gated, topic );
        JoinTopic( *sealer, topic );
        JoinTopic( *rawp, topic );

        // BOTH writers are members (the raw publisher's denial cannot be
        // attributed to membership).
        membership->members.insert( idSealer.toBase58() );
        membership->members.insert( idRaw.toBase58() );

        auto broadcaster_gated = gated->db->GetBroadcaster();
        ASSERT_NE( broadcaster_gated, nullptr );
        broadcaster_gated->SetMembershipFilter( MakeSharedSetFilter( membership ) );
        broadcaster_gated->SetGossipSigningKey( gated->signing_key );

        auto broadcaster_sealer = sealer->db->GetBroadcaster();
        ASSERT_NE( broadcaster_sealer, nullptr );
        broadcaster_sealer->SetMembershipFilter( MakeSharedSetFilter( membership ) );
        broadcaster_sealer->SetGossipSigningKey( sealer->signing_key );

        // The raw publisher installs NOTHING: no filter -> its publishes stay
        // raw (the unsigned-member shape under a gated receiver).
        auto broadcaster_raw = rawp->db->GetBroadcaster();
        ASSERT_NE( broadcaster_raw, nullptr );
        EXPECT_FALSE( broadcaster_raw->HasMembershipFilter() );

        gated->pubsub->AddPeers( { sealer->pubsub->GetInterfaceAddress() } );
        sealer->pubsub->AddPeers( { gated->pubsub->GetInterfaceAddress() } );
        gated->pubsub->AddPeers( { rawp->pubsub->GetInterfaceAddress() } );
        rawp->pubsub->AddPeers( { gated->pubsub->GetInterfaceAddress() } );

        std::thread io_thread( [io_context]() { io_context->run(); } );

        ASSERT_WAIT_FOR_CONDITION(
            [&]()
            {
                return IsConnectedTo( gated->pubsub, idSealer ) && IsConnectedTo( gated->pubsub, idRaw );
            },
            std::chrono::milliseconds( 15000 ),
            "members did not connect to the gated node",
            nullptr );

        // (a) Positive control: the sealed member's write replicates.
        CommitPut( *sealer->db, key_sealed, { 0xde, 0xad, 0xbe, 0xef }, topic );
        assertWaitForCondition(
            [&]()
            {
                auto result = gated->db->Get( key_sealed );
                return result.has_value();
            },
            std::chrono::milliseconds( 20000 ),
            "sealed member write did not replicate to the gated node" );

        // (b) The raw member's write: no envelope -> NOT_AN_ENVELOPE denial
        //     under the set filter (fail-closed), despite membership.
        CommitPut( *rawp->db, key_raw, { 0x13, 0x37, 0x13, 0x37 }, topic );
        AssertKeyNeverPresentWithin( *gated->db,
                                     key_raw,
                                     std::chrono::milliseconds( 3000 ),
                                     "unsigned member write" );

        broadcaster_sealer->ClearMembershipFilter();
        TearDownNodes( broadcaster_gated, io_context, io_thread,
                       { gated->pubsub, sealer->pubsub, rawp->pubsub } );
    }

} // namespace
