/**
 * @file       securecrdt_propose_sign_quorum_test.cpp
 * @brief      SCRDT-04: proves the full propose+sign+quorum sequence, using
 *             only SecureCrdt::ProposeValue/AddSignature/ReadIfQuorum -- no
 *             raw GlobalDB::Put/Get calls appear in this test body.
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
        "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eac0",
        "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eac1",
    };

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

    class SecureCrdtProposeSignQuorumTest : public ::testing::Test
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
            signer_set_static() = signer_set_;

            node_ = sgns::test::securecrdt::MakeSecureCrdtTestNode( "securecrdt_propose_sign_quorum" );
            ASSERT_NE( node_, nullptr );

            SecureCrdtRegistry::Register( "gnus-test-propose-sign-quorum",
                                          SecureCrdtRegistryEntry{
                                              "gnus-test-propose-sign-quorum",
                                              []( const std::string & ) -> outcome::result<SignerSetSnapshot>
                                              { return SignerSetSnapshot{ signer_set_static(), 2 }; },
                                              []() -> std::shared_ptr<ISignedCRDTData>
                                              { return std::make_shared<TestSignedData>(); },
                                              std::regex(), &token_ } );

            secure_crdt_ = std::make_shared<SecureCrdt>( node_->db, "securecrdt_test_topic" );
        }

        void TearDown() override
        {
            SecureCrdtRegistry::UnregisterIf( "gnus-test-propose-sign-quorum", &token_ );
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

TEST_F( SecureCrdtProposeSignQuorumTest, FullProposeSignQuorumSequenceReturnsValueOnlyAtThreshold )
{
    const sgns::crdt::HierarchicalKey base_key( "gnus-test-propose-sign-quorum" );
    const std::vector<uint8_t>        payload = { 'p', 'a', 'y', 'l', 'o', 'a', 'd' };

    auto propose_result = secure_crdt_->ProposeValue( base_key, payload );
    ASSERT_FALSE( propose_result.has_error() ) << propose_result.error().message();

    auto sig1 = signers_[0]->Sign( payload );
    auto add1 = secure_crdt_->AddSignature( base_key, signers_[0]->GetAddress(), sig1 );
    ASSERT_FALSE( add1.has_error() ) << add1.error().message();

    auto read_after_one = secure_crdt_->ReadIfQuorum( base_key );
    ASSERT_FALSE( read_after_one.has_error() );
    EXPECT_FALSE( read_after_one.value().has_value() ) << "1 signature < threshold 2 must never report quorum met";

    auto sig2 = signers_[1]->Sign( payload );
    auto add2 = secure_crdt_->AddSignature( base_key, signers_[1]->GetAddress(), sig2 );
    ASSERT_FALSE( add2.has_error() ) << add2.error().message();

    auto read_after_two = secure_crdt_->ReadIfQuorum( base_key );
    ASSERT_FALSE( read_after_two.has_error() );
    ASSERT_TRUE( read_after_two.value().has_value() ) << "2 signatures >= threshold 2 must report quorum met";
    EXPECT_EQ( read_after_two.value()->toVector(), payload );
}
