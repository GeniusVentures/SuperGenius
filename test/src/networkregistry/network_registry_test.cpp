/**
 * @file       network_registry_test.cpp
 * @brief      D-06/D-03: proves the NetworkRegistry trust lifecycle on a real
 *             single-node SecureCrdt fixture -- TPR-majority bootstrap
 *             (under-signed bootstrap never confirms), post-confirmation
 *             self-governance from the network's own cached member quorum,
 *             rejection of unilateral self-admission, secret-free serialized
 *             records, and the cached TPR-snapshot signer resolution before
 *             confirmation.
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <boost/filesystem/operations.hpp>

#include "account/GeniusAccount.hpp"
#include "base/util.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "networkregistry/NetworkRegistry.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "securecrdt/securecrdt_test_node.hpp"
#include "testutil/wait_condition.hpp"
#include "trustedpeer/TrustedPeerRegistry.hpp"

namespace
{
    using namespace sgns;
    using namespace sgns::networkregistry;

    constexpr const char *TPR_PRIVATE_KEYS[] = {
        "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eac2",
        "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eac3",
        "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eac4",
    };

    constexpr const char *MEMBER_PRIVATE_KEYS[] = {
        "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05ead2",
        "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05ead3",
        "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05ead4",
    };

    constexpr const char *OUTSIDER_PRIVATE_KEY =
        "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05ead5";

    // 0x-hex 32B private-network identity (D-01/D-02 shape from 15-01).
    const std::string kPrivateNetworkId = "0x4e6574776f726b526567697374727954657374496431323334353637383940";

    // Distinctive 32-byte test PSK sentinel (never a production credential):
    // the exact byte string that must NEVER appear inside a membership record.
    const std::string kSentinelPnetSecret = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";

    // Short hex fingerprint of the (absent) credential -- metadata only (D-03).
    const std::string kPnetKeyFingerprint = "a1b2c3d4e5f60718";

    const std::vector<std::string> kInitialPeers = {
        "12D3KooWJcRGbKZXUDxFUCjkfB2GrEHeMd2QArGhHCKRIAwrP",
        "12D3KooWKhyzo3KdTZ3gHpC1x7zyCuD9QAycBDBskuZtSTPcS",
        "12D3KooWMtQvX7eFtK8khzzgeRfZLdEFh2gRtPYQmXbbFgHiA",
    };

    const std::string kNewPeerId = "12D3KooWQkOLJrUzPZFTIDsBeCXVhV2srjcQhHyBZ4TPXCeVoW";

    /// @brief Bounded negative window helper: true when the registry does NOT
    ///        newly confirm anything within `window` (used to prove
    ///        under-signed records NEVER confirm, without raw sleeps).
    template <typename Registry>
    bool NeverConfirmsWithin( const std::shared_ptr<Registry> &registry, std::chrono::milliseconds window )
    {
        return !waitForCondition(
            [&registry]()
            {
                auto result = registry->TryConfirm();
                return result.has_value() && result.value();
            },
            window );
    }

    class NetworkRegistryTest : public ::testing::Test
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
            for ( const char *key : MEMBER_PRIVATE_KEYS )
            {
                auto account = GeniusAccount::NewFromPrivateKey( TokenID::FromBytes( { 0x00 } ), key, path_ );
                member_accounts_.push_back( account );
                member_signers_.push_back( account->GetAddress() );
            }
            outsider_ = GeniusAccount::NewFromPrivateKey( TokenID::FromBytes( { 0x00 } ), OUTSIDER_PRIVATE_KEY, path_ );

            node_ = sgns::test::securecrdt::MakeSecureCrdtTestNode( "networkregistry" );
            ASSERT_NE( node_, nullptr );

            secure_crdt_ = std::make_shared<sgns::securecrdt::SecureCrdt>( node_->db, "networkregistry-test" );
            secure_crdt_->RegisterFilters();

            // Global root trust domain (D-05): a 3-peer TrustedPeerRegistry
            // serves as the bootstrap authority for every NetworkRegistry.
            auto tpr_result = sgns::trustedpeer::TrustedPeerRegistry::New(
                secure_crdt_, tpr_peers_, tpr_peers_[0], /*threshold=*/2 );
            ASSERT_FALSE( tpr_result.has_error() ) << tpr_result.error().message();
            tpr_ = tpr_result.value();
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

        /// @brief Builds the unit under test: 3 PeerId members, 3 member
        ///        signers, self-governance quorum 2, metadata-only fingerprint.
        ///        The CRDT change-callback (async cache refresh) is enabled
        ///        only when `with_change_callback` -- the explicit lifecycle
        ///        tests exercise deterministic TryConfirm semantics, and a
        ///        dedicated test covers the callback-driven refresh.
        void MakeRegistry( bool with_change_callback = false )
        {
            auto new_result = NetworkRegistry::New( secure_crdt_,
                                                    tpr_,
                                                    kPrivateNetworkId,
                                                    kInitialPeers,
                                                    /*network_quorum_threshold=*/2,
                                                    member_signers_,
                                                    kPnetKeyFingerprint,
                                                    with_change_callback ? node_->db : nullptr );
            ASSERT_FALSE( new_result.has_error() ) << new_result.error().message();
            registry_ = new_result.value();
        }

        /// @brief Seeds the bootstrap record and adds `signature_count`
        ///        genuine TPR-member signatures over the proposed bytes.
        void SeedAndSignBootstrap( size_t signature_count )
        {
            auto seed_result = registry_->SeedBootstrap( kInitialPeers );
            ASSERT_FALSE( seed_result.has_error() ) << seed_result.error().message();

            const auto proposed = ProposedRecordBytes();
            ASSERT_TRUE( proposed.has_value() );
            for ( size_t i = 0; i < signature_count && i < tpr_accounts_.size(); ++i )
            {
                const auto signature = tpr_accounts_[i]->Sign( *proposed );
                auto sign_result = registry_->SignMembershipChange( tpr_accounts_[i]->GetAddress(), signature );
                ASSERT_FALSE( sign_result.has_error() ) << sign_result.error().message();
            }
        }

        /// @brief Confirms the bootstrap with a full TPR majority (2 of 3).
        void ConfirmBootstrapWithTprMajority()
        {
            MakeRegistry();
            SeedAndSignBootstrap( /*signature_count=*/2 );
            auto confirm_result = registry_->TryConfirm();
            ASSERT_FALSE( confirm_result.has_error() ) << confirm_result.error().message();
            ASSERT_TRUE( confirm_result.value() );
            ASSERT_TRUE( registry_->IsBootstrapConfirmed() );
        }

        /// @brief Reads the currently-proposed record bytes straight from the
        ///        GlobalDB (signatures must be over exactly these bytes).
        std::optional<std::vector<uint8_t>> ProposedRecordBytes()
        {
            auto value = node_->db->Get( registry_->BaseKey() );
            if ( value.has_error() )
            {
                return std::nullopt;
            }
            return value.value().toVector();
        }

        boost::filesystem::path                        path_;
        std::vector<std::shared_ptr<GeniusAccount>>    tpr_accounts_;
        std::vector<std::string>                       tpr_peers_;
        std::vector<std::shared_ptr<GeniusAccount>>    member_accounts_;
        std::vector<std::string>                       member_signers_;
        std::shared_ptr<GeniusAccount>                 outsider_;
        std::unique_ptr<sgns::test::securecrdt::SecureCrdtTestNode> node_;
        std::shared_ptr<sgns::securecrdt::SecureCrdt>  secure_crdt_;
        std::shared_ptr<sgns::trustedpeer::TrustedPeerRegistry>     tpr_;
        std::shared_ptr<NetworkRegistry>                              registry_;
        std::chrono::milliseconds                                     refresh_wait_{ 0 };
    };

    //
    // Payload unit tests (codec + structural Verify, D-03).
    //

    // A valid 128-hex member account address (structural shape only).
    const std::string kValidSignerAddress =
        "8a33bdf1445a68736429d1773be8682362753a0efc6fb9d8b3e8dffe3b74fc91e26b203fd521547a5219eddf1d3ac51fd17a7646c"
        "9bca5ef065da131add4e5a2";

    TEST( NetworkMembershipPayloadTest, RoundTripPreservesAllFields )
    {
        const NetworkMembershipPayload original( kInitialPeers, { kValidSignerAddress }, 7, kPnetKeyFingerprint );
        const auto                     bytes = original.SerializeToBytes();

        const auto decoded = NetworkMembershipPayload::FromBytes( bytes );

        ASSERT_TRUE( decoded.has_value() );
        EXPECT_EQ( decoded->GetNetworkPeers(), kInitialPeers );
        EXPECT_EQ( decoded->GetNetworkSigners(), std::vector<std::string>{ kValidSignerAddress } );
        EXPECT_EQ( decoded->GetPnetKeyVersion(), 7u );
        EXPECT_EQ( decoded->GetPnetKeyFingerprint(), kPnetKeyFingerprint );
        EXPECT_TRUE( decoded->Verify( bytes ) );
    }

    TEST( NetworkMembershipPayloadTest, FromBytesRejectsEmptyAndGarbageInput )
    {
        EXPECT_FALSE( NetworkMembershipPayload::FromBytes( {} ).has_value() );
        const std::vector<uint8_t> garbage = { 'n', 'o', 't', '-', 'a', '-', 'r', 'e', 'c', 'o', 'r', 'd' };
        EXPECT_FALSE( NetworkMembershipPayload::FromBytes( garbage ).has_value() );
    }

    TEST( NetworkMembershipPayloadTest, VerifyRejectsDuplicatesNonPeerIdEntriesAndBadSigners )
    {
        const std::vector<std::string> duplicated = { kInitialPeers[0], kInitialPeers[0], kInitialPeers[1] };
        const auto                     dup_bytes = NetworkMembershipPayload( duplicated, {}, 1, {} ).SerializeToBytes();
        EXPECT_FALSE( NetworkMembershipPayload( duplicated, {}, 1, {} ).Verify( dup_bytes ) );

        const std::vector<std::string> not_peer_ids = { "not-a-peer-id", kInitialPeers[1] };
        const auto bad_bytes = NetworkMembershipPayload( not_peer_ids, {}, 1, {} ).SerializeToBytes();
        EXPECT_FALSE( NetworkMembershipPayload( not_peer_ids, {}, 1, {} ).Verify( bad_bytes ) );

        const std::vector<std::string> bad_signers = { "deadbeef" }; // not a 128-hex address
        const auto signer_bytes =
            NetworkMembershipPayload( kInitialPeers, bad_signers, 1, {} ).SerializeToBytes();
        EXPECT_FALSE( NetworkMembershipPayload( kInitialPeers, bad_signers, 1, {} ).Verify( signer_bytes ) );

        // Empty membership is structurally invalid.
        const auto empty_bytes = NetworkMembershipPayload( {}, {}, 1, {} ).SerializeToBytes();
        EXPECT_FALSE( NetworkMembershipPayload( {}, {}, 1, {} ).Verify( empty_bytes ) );
    }

    //
    // (1) Bootstrap confirms only at a TPR majority (D-06, T-15-07).
    //

    TEST_F( NetworkRegistryTest, BootstrapUnderTprMajorityConfirms )
    {
        MakeRegistry();
        EXPECT_FALSE( registry_->IsBootstrapConfirmed() );
        EXPECT_EQ( registry_->GetCurrentPeers(), kInitialPeers );

        // 1 of 3 TPR signatures -- strictly below the ceil(0.51*3) majority.
        SeedAndSignBootstrap( /*signature_count=*/1 );

        EXPECT_TRUE( NeverConfirmsWithin( registry_, std::chrono::milliseconds( 300 ) ) )
            << "1-of-3 TPR signatures must NEVER confirm the bootstrap within the bounded window";
        EXPECT_FALSE( registry_->IsBootstrapConfirmed() );
        EXPECT_EQ( registry_->GetCurrentPeers(), kInitialPeers ) << "cache must remain the initial membership";

        // 2nd TPR signature -- the strict-majority quorum (ceil(0.51*3) = 2).
        const auto proposed = ProposedRecordBytes();
        ASSERT_TRUE( proposed.has_value() );
        const auto signature = tpr_accounts_[1]->Sign( *proposed );
        auto sign_result = registry_->SignMembershipChange( tpr_accounts_[1]->GetAddress(), signature );
        ASSERT_FALSE( sign_result.has_error() ) << sign_result.error().message();

        auto confirm_result = registry_->TryConfirm();
        ASSERT_FALSE( confirm_result.has_error() ) << confirm_result.error().message();
        EXPECT_TRUE( confirm_result.value() ) << "2-of-3 TPR signatures satisfy the bootstrap quorum";
        EXPECT_TRUE( registry_->IsBootstrapConfirmed() );
        EXPECT_EQ( registry_->GetCurrentPeers(), kInitialPeers ) << "cache reflects the confirmed record";
    }

    //
    // (2) Post-confirmation self-governance at the network's own quorum (D-06, T-15-08).
    //

    TEST_F( NetworkRegistryTest, SelfGovernanceAfterConfirm )
    {
        ConfirmBootstrapWithTprMajority();

        const std::vector<std::string> new_peers   = { kInitialPeers[0], kInitialPeers[2], kNewPeerId };
        const std::vector<std::string> new_signers = { member_signers_[0], member_signers_[2] };

        auto propose_result = registry_->ProposeMembershipChange( new_peers, new_signers );
        ASSERT_FALSE( propose_result.has_error() ) << propose_result.error().message();

        // Sub-quorum (1 of the cached member signers, quorum 2): never confirms.
        const auto proposed = ProposedRecordBytes();
        ASSERT_TRUE( proposed.has_value() );
        const auto sig0 = member_accounts_[0]->Sign( *proposed );
        auto sign0 = registry_->SignMembershipChange( member_signers_[0], sig0 );
        ASSERT_FALSE( sign0.has_error() ) << sign0.error().message();

        EXPECT_TRUE( NeverConfirmsWithin( registry_, std::chrono::milliseconds( 300 ) ) )
            << "a single member signature must NEVER confirm a membership change";
        EXPECT_EQ( registry_->GetCurrentPeers(), kInitialPeers ) << "sub-quorum must leave membership untouched";

        // Quorum met: a second cached member signer confirms and the cache updates.
        const auto sig1 = member_accounts_[1]->Sign( *proposed );
        auto sign1 = registry_->SignMembershipChange( member_signers_[1], sig1 );
        ASSERT_FALSE( sign1.has_error() ) << sign1.error().message();

        auto confirm_result = registry_->TryConfirm();
        ASSERT_FALSE( confirm_result.has_error() ) << confirm_result.error().message();
        EXPECT_TRUE( confirm_result.value() );
        EXPECT_EQ( registry_->GetCurrentPeers(), new_peers ) << "quorum-met change must replace the membership";

        // Self-governance switched the signer authority to the record's own signers.
        auto snapshot = registry_->CurrentSignerSet();
        ASSERT_FALSE( snapshot.has_error() ) << snapshot.error().message();
        EXPECT_EQ( snapshot.value().signer_set, new_signers );
        EXPECT_EQ( snapshot.value().required_signatures, 2u );
    }

    //
    // (3) No unilateral self-admission (D-06, T-15-08).
    //

    TEST_F( NetworkRegistryTest, SinglePeerCannotAdmitItself )
    {
        ConfirmBootstrapWithTprMajority();

        const std::vector<std::string> widened_peers = { kInitialPeers[0], kInitialPeers[1], kNewPeerId };
        const std::vector<std::string> widened_signers = { member_signers_[0], member_signers_[1], outsider_->GetAddress() };

        auto propose_result = registry_->ProposeMembershipChange( widened_peers, widened_signers );
        ASSERT_FALSE( propose_result.has_error() ) << propose_result.error().message();
        const auto proposed = ProposedRecordBytes();
        ASSERT_TRUE( proposed.has_value() );

        // Cryptographically valid signature, but from an address that is NOT
        // in the current signer set -- rejected before persistence.
        const auto outsider_sig = outsider_->Sign( *proposed );
        auto outsider_sign = registry_->SignMembershipChange( outsider_->GetAddress(), outsider_sig );
        EXPECT_TRUE( outsider_sign.has_error() );
        EXPECT_EQ( outsider_sign.error().message(),
                   "signer is noncanonical or absent from the current signer-set snapshot" );

        EXPECT_TRUE( NeverConfirmsWithin( registry_, std::chrono::milliseconds( 300 ) ) )
            << "a self-admission signature must NEVER confirm";
        const auto current_membership = registry_->GetCurrentPeers();
        EXPECT_EQ( std::find( current_membership.begin(), current_membership.end(), kNewPeerId ),
                   current_membership.end() )
            << "membership must remain unchanged";

        // Even one genuine member signature stays below quorum: still no admission.
        const auto member_sig = member_accounts_[0]->Sign( *proposed );
        auto member_sign = registry_->SignMembershipChange( member_signers_[0], member_sig );
        ASSERT_FALSE( member_sign.has_error() ) << member_sign.error().message();

        EXPECT_TRUE( NeverConfirmsWithin( registry_, std::chrono::milliseconds( 300 ) ) )
            << "1 member signature + a rejected self-admission signature is still sub-quorum";
        EXPECT_EQ( registry_->GetCurrentPeers(), kInitialPeers );
    }

    //
    // (4) Serialized records carry metadata only -- never raw credential
    //     material (D-03, T-15-09).
    //

    TEST_F( NetworkRegistryTest, NoRawKeyMaterialInRecords )
    {
        MakeRegistry();
        auto seed_result = registry_->SeedBootstrap( kInitialPeers );
        ASSERT_FALSE( seed_result.has_error() ) << seed_result.error().message();

        // The persisted CRDT record bytes:
        const auto stored = node_->db->Get( registry_->BaseKey() );
        ASSERT_FALSE( stored.has_error() ) << stored.error().message();
        const std::string record_bytes( stored.value().toVector().begin(), stored.value().toVector().end() );

        // The distinctive 32-byte test PSK (and any 16-char slice of it) is
        // nowhere in the record.
        EXPECT_EQ( record_bytes.find( kSentinelPnetSecret ), std::string::npos )
            << "raw pnet credential material must never appear in a membership record";
        const std::string sentinel_slice = kSentinelPnetSecret.substr( 8, 16 );
        EXPECT_EQ( record_bytes.find( sentinel_slice ), std::string::npos );

        // The non-secret metadata fields ARE carried (version + fingerprint).
        EXPECT_NE( record_bytes.find( "VERSION 1" ), std::string::npos );
        EXPECT_NE( record_bytes.find( "FINGERPRINT " + kPnetKeyFingerprint ), std::string::npos );

        // PeerId membership entries are carried (the gater allow-list payload).
        for ( const auto &peer : kInitialPeers )
        {
            EXPECT_NE( record_bytes.find( peer ), std::string::npos );
        }

        // Same guarantees on the direct payload serialization path.
        const auto payload_bytes =
            NetworkMembershipPayload( kInitialPeers, member_signers_, 1, kPnetKeyFingerprint ).SerializeToBytes();
        const std::string payload_str( payload_bytes.begin(), payload_bytes.end() );
        EXPECT_EQ( payload_str.find( kSentinelPnetSecret ), std::string::npos );
    }

    //
    // (5) Pre-confirmation signer resolution uses the TPR snapshot (D-06, T-15-10).
    //

    TEST_F( NetworkRegistryTest, UnconfirmedResolveUsesTprSnapshot )
    {
        MakeRegistry();

        auto snapshot = registry_->CurrentSignerSet();
        ASSERT_FALSE( snapshot.has_error() ) << snapshot.error().message();
        EXPECT_EQ( snapshot.value().signer_set, tpr_peers_ )
            << "pre-confirmation authority is the global TPR's current peers";
        EXPECT_EQ( snapshot.value().required_signatures, 2u )
            << "bootstrap requires the TPR strict majority ceil(0.51*3) = 2";

        EXPECT_EQ( registry_->BaseKey().GetKey(), NetworkRegistry::DefaultBaseKey( kPrivateNetworkId ).GetKey() );
        EXPECT_FALSE( registry_->IsBootstrapConfirmed() );
    }

    //
    // (6) Change-callback cache refresh: a quorum-signed bootstrap record
    //     confirms WITHOUT any explicit TryConfirm call (BurnConfig-pattern
    //     refresh, executed off the datastore callback thread), and an
    //     under-signed record never refreshes the cache.
    //

    TEST_F( NetworkRegistryTest, CacheRefreshViaCrdtChangeCallback )
    {
        MakeRegistry( /*with_change_callback=*/true );
        EXPECT_FALSE( registry_->IsBootstrapConfirmed() );

        // 1 of 3 TPR signatures: the async refresh must NOT confirm.
        SeedAndSignBootstrap( /*signature_count=*/1 );
        EXPECT_FALSE( waitForCondition(
            [this]() { return registry_->IsBootstrapConfirmed(); }, std::chrono::milliseconds( 300 ) ) )
            << "1-of-3 TPR signatures must never confirm, not even via the change callback";

        // 2nd TPR signature: quorum met -- the change callback refreshes the
        // cache on its own (no explicit TryConfirm anywhere in this test).
        const auto proposed = ProposedRecordBytes();
        ASSERT_TRUE( proposed.has_value() );
        const auto signature = tpr_accounts_[1]->Sign( *proposed );
        auto sign_result = registry_->SignMembershipChange( tpr_accounts_[1]->GetAddress(), signature );
        ASSERT_FALSE( sign_result.has_error() ) << sign_result.error().message();

        ASSERT_WAIT_FOR_CONDITION( [this]() { return registry_->IsBootstrapConfirmed(); },
                                   std::chrono::milliseconds( 2000 ),
                                   "change callback must refresh the cache once the TPR majority is met",
                                   &refresh_wait_ );
        EXPECT_EQ( registry_->GetCurrentPeers(), kInitialPeers );
    }
} // namespace
