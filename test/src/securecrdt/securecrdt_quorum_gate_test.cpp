/**
 * @file       securecrdt_quorum_gate_test.cpp
 * @brief      SCRDT-03: proves an under-signed write never causes ReadIfQuorum
 *             to report quorum met, and proves ProposeValue rejects malformed
 *             payloads locally (Warning 1 closure, T-09-10).
 */
#include <gtest/gtest.h>

#include <boost/filesystem/operations.hpp>

#include <cctype>

#include "account/GeniusAccount.hpp"
#include "account/GeniusSigner.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "securecrdt_test_node.hpp"
#include "testutil/wait_condition.hpp"

namespace
{
    using namespace sgns;
    using namespace sgns::securecrdt;

    constexpr const char *PRIVATE_KEYS[] = {
        "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eab0",
        "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eab1",
        "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eab2",
    };

    /// @brief Mirrors securecrdt_interface_test.cpp's TestSignedData: empty
    ///        bytes are the malformed-input sentinel.
    class TestSignedData : public ISignedCRDTData
    {
    public:
        std::vector<uint8_t> SerializeToBytes() const override
        {
            return value_;
        }

        bool DeserializeFromBytes( const std::vector<uint8_t> &bytes ) override
        {
            if ( bytes.empty() )
            {
                return false;
            }
            value_ = bytes;
            return true;
        }

        bool Verify( const std::vector<uint8_t> &payload ) const override
        {
            return !payload.empty();
        }

        void Apply() override
        {
            applied_ = true;
        }

        std::vector<uint8_t> value_;
        bool                 applied_ = false;
    };

    class SecureCrdtQuorumGateTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                { return std::make_shared<MemorySecureStorage>( identifier ); } );
            path_ = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();

            for ( const char *key : PRIVATE_KEYS )
            {
                auto account = GeniusAccount::NewFromPrivateKey( TokenID::FromBytes( { 0x00 } ), key, path_ );
                signers_.push_back( account );
                signer_set_.push_back( account->GetAddress() );
            }

            node_ = sgns::test::securecrdt::MakeSecureCrdtTestNode( "securecrdt_quorum_gate" );
            ASSERT_NE( node_, nullptr );

            secure_crdt_ = std::make_shared<SecureCrdt>( node_->db, "securecrdt_test_topic" );
            ASSERT_TRUE( secure_crdt_->Registry().Register( "gnus-test-secure",
                                          SecureCrdtRegistryEntry{
                                              "gnus-test-secure",
                                              []( const std::string & ) -> outcome::result<SignerSetSnapshot>
                                              { return SignerSetSnapshot{ signer_set_static(), 2 }; },
                                              []() -> std::shared_ptr<ISignedCRDTData>
                                              { return std::make_shared<TestSignedData>(); },
                                              std::regex(), &token_ } ) );

            ASSERT_TRUE( secure_crdt_->Registry().Register( "gnus-test-secure-malformed",
                                          SecureCrdtRegistryEntry{
                                              "gnus-test-secure-malformed",
                                              []( const std::string & ) -> outcome::result<SignerSetSnapshot>
                                              { return SignerSetSnapshot{ signer_set_static(), 2 }; },
                                              []() -> std::shared_ptr<ISignedCRDTData>
                                              { return std::make_shared<TestSignedData>(); },
                                              std::regex(), &token_ } ) );
        }

        void TearDown() override
        {
            secure_crdt_->Registry().UnregisterIf( "gnus-test-secure", &token_ );
            secure_crdt_->Registry().UnregisterIf( "gnus-test-secure-malformed", &token_ );
            secure_crdt_.reset();
            node_.reset();
            GeniusAccount::SetSecureStorageFactory( nullptr );
            boost::filesystem::remove_all( path_ );
        }

        static std::vector<std::string> &signer_set_static()
        {
            static std::vector<std::string> set;
            return set;
        }

        boost::filesystem::path                     path_;
        std::vector<std::shared_ptr<GeniusAccount>> signers_;
        std::vector<std::string>                    signer_set_;
        int                                          token_ = 0;
        std::unique_ptr<sgns::test::securecrdt::SecureCrdtTestNode> node_;
        std::shared_ptr<SecureCrdt>                                secure_crdt_;
    };
} // namespace

TEST_F( SecureCrdtQuorumGateTest, UnderSignedWriteNeverReportsQuorum )
{
    signer_set_static() = signer_set_;
    const sgns::crdt::HierarchicalKey base_key( "gnus-test-secure" );
    const std::vector<uint8_t>        payload = { 'v', 'a', 'l', 'u', 'e' };

    auto propose_result = secure_crdt_->ProposeValue( base_key, payload );
    ASSERT_FALSE( propose_result.has_error() ) << propose_result.error().message();

    auto read1 = secure_crdt_->ReadIfQuorum( base_key );
    ASSERT_FALSE( read1.has_error() );
    EXPECT_FALSE( read1.value().has_value() );

    auto sig1 = signers_[0]->Sign( payload );
    auto add1 = secure_crdt_->AddSignature( base_key, signers_[0]->GetAddress(), sig1 );
    ASSERT_FALSE( add1.has_error() ) << add1.error().message();

    auto read2 = secure_crdt_->ReadIfQuorum( base_key );
    ASSERT_FALSE( read2.has_error() );
    EXPECT_FALSE( read2.value().has_value() ) << "1 signature < threshold 2 must never report quorum met";

    const std::vector<uint8_t> invalid_signature = { 'g', 'a', 'r', 'b', 'a', 'g', 'e' };
    auto add2 = secure_crdt_->AddSignature( base_key, signers_[1]->GetAddress(), invalid_signature );
    EXPECT_TRUE( add2.has_error() );
    EXPECT_EQ( add2.error(), SecureCrdt::Error::INVALID_SIGNATURE );

    auto read3 = secure_crdt_->ReadIfQuorum( base_key );
    ASSERT_FALSE( read3.has_error() );
    EXPECT_FALSE( read3.value().has_value() ) << "invalid signature must never be persisted as trusted state";
}

TEST_F( SecureCrdtQuorumGateTest, ProposeValueRejectsMalformedPayloadLocally )
{
    signer_set_static() = signer_set_;
    const sgns::crdt::HierarchicalKey base_key( "gnus-test-secure-malformed" );

    auto propose_result = secure_crdt_->ProposeValue( base_key, {} );
    ASSERT_TRUE( propose_result.has_error() );
    EXPECT_EQ( propose_result.error(), SecureCrdt::Error::MALFORMED_VALUE );

    auto get_result = node_->db->Get( base_key );
    EXPECT_TRUE( get_result.has_error() ) << "db_->Put must never be called for a malformed local proposal";
}

TEST_F( SecureCrdtQuorumGateTest, LocalOutsiderSignatureNeverPersists )
{
    signer_set_static() = { signer_set_[0], signer_set_[1] };
    const sgns::crdt::HierarchicalKey base_key( "gnus-test-secure" );
    const std::vector<uint8_t>        payload = { 'l', 'o', 'c', 'a', 'l' };
    ASSERT_FALSE( secure_crdt_->ProposeValue( base_key, payload ).has_error() );

    const auto outsider_signature = signers_[2]->Sign( payload );
    const auto outsider_result =
        secure_crdt_->AddSignature( base_key, signers_[2]->GetAddress(), outsider_signature );
    EXPECT_TRUE( outsider_result.has_error() );
    EXPECT_TRUE( node_->db->Get( base_key.ChildString( "sig" ).ChildString( signers_[2]->GetAddress() ) ).has_error() );

    std::string mixed_case = signers_[0]->GetAddress();
    mixed_case.front()     = static_cast<char>( std::toupper( mixed_case.front() ) );
    const auto mixed_case_result = secure_crdt_->AddSignature( base_key, mixed_case, signers_[0]->Sign( payload ) );
    EXPECT_TRUE( mixed_case_result.has_error() );
    EXPECT_TRUE( node_->db->Get( base_key.ChildString( "sig" ).ChildString( mixed_case ) ).has_error() );

    const auto retained = node_->db->QueryKeyValues( base_key.ChildString( "sig" ).GetKey() );
    ASSERT_TRUE( retained.has_value() );
    EXPECT_TRUE( retained.value().empty() );
}

TEST_F( SecureCrdtQuorumGateTest, RemoteOutsiderSignatureNeverReplicatesAndRetentionBoundTracksAuthorizedSet )
{
    constexpr const char                  *topic = "securecrdt_test_topic";
    const sgns::crdt::HierarchicalKey      base_key( "gnus-test-secure" );
    const std::vector<uint8_t>             payload = { 'r', 'e', 'm', 'o', 't', 'e' };
    signer_set_static() = { signer_set_[0], signer_set_[1] };

    ASSERT_FALSE( node_->db->AddBroadcastTopic( topic ).has_error() );
    ASSERT_TRUE( secure_crdt_->RegisterFilters() );

    auto attacker_node = sgns::test::securecrdt::MakeSecureCrdtTestNode( "securecrdt_quorum_outsider" );
    ASSERT_NE( attacker_node, nullptr );
    ASSERT_FALSE( attacker_node->db->AddBroadcastTopic( topic ).has_error() );
    attacker_node->db->AddListenTopic( topic );
    attacker_node->pubsub->AddPeers( { node_->pubsub->GetInterfaceAddress() } );
    sgns::test::assertWaitForCondition(
        [&]
        {
            return !attacker_node->pubsub->GetHost()->getNetwork().getConnectionManager().getConnections().empty();
        },
        std::chrono::seconds( 25 ),
        "SecureCrdt outsider peer did not connect" );

    ASSERT_FALSE( secure_crdt_->ProposeValue( base_key, payload ).has_error() );
    sgns::test::assertWaitForCondition(
        [&]
        {
            const auto replicated = attacker_node->db->Get( base_key );
            return replicated.has_value() && replicated.value().toVector() == payload;
        },
        std::chrono::seconds( 25 ),
        "base value did not replicate to the adversarial peer" );

    const auto outsider = GeniusSigner::Generate();
    ASSERT_FALSE( attacker_node->db
                      ->Put( base_key.ChildString( "sig" ).ChildString( outsider.GetAddress() ),
                             sgns::base::Buffer( outsider.Sign( payload ) ),
                             { topic } )
                      .has_error() );
    ASSERT_FALSE( attacker_node->db
                      ->Put( base_key.ChildString( "sig" ).ChildString( signers_[0]->GetAddress() ),
                             sgns::base::Buffer( signers_[0]->Sign( payload ) ),
                             { topic } )
                      .has_error() );

    const auto authorized_key = base_key.ChildString( "sig" ).ChildString( signers_[0]->GetAddress() );
    sgns::test::assertWaitForCondition( [&] { return node_->db->Get( authorized_key ).has_value(); },
                                        std::chrono::seconds( 25 ),
                                        "authorized signature did not replicate" );
    EXPECT_TRUE( node_->db->Get( base_key.ChildString( "sig" ).ChildString( outsider.GetAddress() ) ).has_error() );

    signer_set_static() = signer_set_;
    for ( const auto &signer : signers_ )
    {
        ASSERT_FALSE( secure_crdt_->AddSignature( base_key, signer->GetAddress(), signer->Sign( payload ) ).has_error() );
    }
    for ( size_t index = 0; index < 8; ++index )
    {
        const auto injected = GeniusSigner::Generate();
        ASSERT_FALSE( node_->db
                          ->Put( base_key.ChildString( "sig" ).ChildString( injected.GetAddress() ),
                                 sgns::base::Buffer( injected.Sign( payload ) ),
                                 {} )
                          .has_error() );
    }

    signer_set_static() = { signer_set_[0], signer_set_[1] };
    const auto quorum = secure_crdt_->ReadIfQuorum( base_key );
    ASSERT_TRUE( quorum.has_value() );
    ASSERT_TRUE( quorum.value().has_value() );

    const auto retained = node_->db->QueryKeyValues( base_key.ChildString( "sig" ).GetKey() );
    ASSERT_TRUE( retained.has_value() );
    EXPECT_EQ( retained.value().size(), 2U );
}

TEST_F( SecureCrdtQuorumGateTest, InProcessNodesKeepSameKeyPoliciesAndQuorumIndependent )
{
    constexpr const char *shared_key = "gnus-test-node-isolation";
    const sgns::crdt::HierarchicalKey base_key( shared_key );
    const std::vector<uint8_t>        payload_a = { 'n', 'o', 'd', 'e', '-', 'a' };
    const std::vector<uint8_t>        payload_b = { 'n', 'o', 'd', 'e', '-', 'b' };

    auto node_b = sgns::test::securecrdt::MakeSecureCrdtTestNode( "securecrdt_quorum_gate_peer" );
    ASSERT_NE( node_b, nullptr );
    auto secure_crdt_b = std::make_shared<SecureCrdt>( node_b->db, "securecrdt_test_topic" );
    int  token_a = 0;
    int  token_b = 0;

    ASSERT_TRUE( secure_crdt_->Registry().Register(
        shared_key,
        SecureCrdtRegistryEntry{
            shared_key,
            [signer = signers_[0]->GetAddress()]( const std::string & ) -> outcome::result<SignerSetSnapshot>
            { return SignerSetSnapshot{ { signer }, 1 }; },
            []() -> std::shared_ptr<ISignedCRDTData> { return std::make_shared<TestSignedData>(); },
            std::regex(),
            &token_a } ) );
    ASSERT_TRUE( secure_crdt_b->Registry().Register(
        shared_key,
        SecureCrdtRegistryEntry{
            shared_key,
            [signer = signers_[1]->GetAddress()]( const std::string & ) -> outcome::result<SignerSetSnapshot>
            { return SignerSetSnapshot{ { signer }, 1 }; },
            []() -> std::shared_ptr<ISignedCRDTData> { return std::make_shared<TestSignedData>(); },
            std::regex(),
            &token_b } ) );

    ASSERT_FALSE( secure_crdt_->ProposeValue( base_key, payload_a ).has_error() );
    ASSERT_FALSE( secure_crdt_b->ProposeValue( base_key, payload_b ).has_error() );
    ASSERT_FALSE( secure_crdt_->AddSignature( base_key, signers_[0]->GetAddress(), signers_[0]->Sign( payload_a ) )
                      .has_error() );
    ASSERT_FALSE( secure_crdt_b
                      ->AddSignature( base_key, signers_[1]->GetAddress(), signers_[1]->Sign( payload_b ) )
                      .has_error() );

    sgns::test::assertWaitForCondition(
        [&]
        {
            const auto read_a = secure_crdt_->ReadIfQuorum( base_key );
            const auto read_b = secure_crdt_b->ReadIfQuorum( base_key );
            return read_a.has_value() && read_a.value().has_value() &&
                   read_a.value()->toVector() == payload_a && read_b.has_value() && read_b.value().has_value() &&
                   read_b.value()->toVector() == payload_b;
        },
        std::chrono::seconds( 5 ),
        "both in-process SecureCrdt nodes did not independently reach quorum" );

    secure_crdt_b->Registry().UnregisterIf( shared_key, &token_b );
    secure_crdt_b.reset();
    node_b.reset();

    const auto retained = secure_crdt_->Registry().Resolve( shared_key );
    ASSERT_TRUE( retained.has_value() );
    const auto retained_snapshot = retained->signer_set_source( shared_key );
    ASSERT_TRUE( retained_snapshot.has_value() );
    EXPECT_EQ( retained_snapshot.value().signer_set,
               std::vector<std::string>{ signers_[0]->GetAddress() } );

    const auto read_after_peer_destroyed = secure_crdt_->ReadIfQuorum( base_key );
    ASSERT_TRUE( read_after_peer_destroyed.has_value() );
    ASSERT_TRUE( read_after_peer_destroyed.value().has_value() );
    EXPECT_EQ( read_after_peer_destroyed.value()->toVector(), payload_a );

    secure_crdt_->Registry().UnregisterIf( shared_key, &token_a );
}
