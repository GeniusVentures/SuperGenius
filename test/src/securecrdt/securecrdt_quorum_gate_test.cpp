/**
 * @file       securecrdt_quorum_gate_test.cpp
 * @brief      SCRDT-03: proves an under-signed write never causes ReadIfQuorum
 *             to report quorum met, and proves ProposeValue rejects malformed
 *             payloads locally (Warning 1 closure, T-09-10).
 */
#include <gtest/gtest.h>

#include <boost/filesystem/operations.hpp>

#include "account/GeniusAccount.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "securecrdt_test_node.hpp"

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

            SecureCrdtRegistry::Register( "gnus-test-secure",
                                          SecureCrdtRegistryEntry{
                                              "gnus-test-secure",
                                              []( const std::string & ) -> outcome::result<SignerSetSnapshot>
                                              { return SignerSetSnapshot{ signer_set_static(), 2 }; },
                                              []() -> std::shared_ptr<ISignedCRDTData>
                                              { return std::make_shared<TestSignedData>(); },
                                              std::regex(), &token_ } );

            SecureCrdtRegistry::Register( "gnus-test-secure-malformed",
                                          SecureCrdtRegistryEntry{
                                              "gnus-test-secure-malformed",
                                              []( const std::string & ) -> outcome::result<SignerSetSnapshot>
                                              { return SignerSetSnapshot{ signer_set_static(), 2 }; },
                                              []() -> std::shared_ptr<ISignedCRDTData>
                                              { return std::make_shared<TestSignedData>(); },
                                              std::regex(), &token_ } );

            secure_crdt_ = std::make_shared<SecureCrdt>( node_->db, "securecrdt_test_topic" );
        }

        void TearDown() override
        {
            SecureCrdtRegistry::UnregisterIf( "gnus-test-secure", &token_ );
            SecureCrdtRegistry::UnregisterIf( "gnus-test-secure-malformed", &token_ );
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
