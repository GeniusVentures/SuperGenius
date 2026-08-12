/**
 * @file       securecrdt_quorum_fixture.hpp
 * @brief      Shared two-signer SecureCRDT quorum test fixture.
 */
#ifndef SGNS_TEST_SECURECRDT_QUORUM_FIXTURE_HPP
#define SGNS_TEST_SECURECRDT_QUORUM_FIXTURE_HPP

#include <boost/filesystem/operations.hpp>
#include <gtest/gtest.h>

#include "account/GeniusAccount.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "securecrdt_test_node.hpp"

namespace sgns::test::securecrdt
{
    class TestSignedData : public sgns::securecrdt::ISignedCRDTData
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

    class SecureCrdtQuorumFixture : public ::testing::Test
    {
    protected:
        static constexpr const char *BASE_KEY       = "gnus-test-quorum";
        static constexpr const char *PRIVATE_KEYS[] = {
            "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05ead0",
            "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05ead1",
        };

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

            node_ = MakeSecureCrdtTestNode( "securecrdt_quorum" );
            ASSERT_NE( node_, nullptr );

            secure_crdt_ = std::make_shared<sgns::securecrdt::SecureCrdt>( node_->db, "securecrdt_test_topic" );
            ASSERT_TRUE( secure_crdt_->Registry().Register(
                BASE_KEY,
                sgns::securecrdt::SecureCrdtRegistryEntry{
                    BASE_KEY,
                    [this]( const std::string & ) -> outcome::result<sgns::securecrdt::SignerSetSnapshot>
                    { return sgns::securecrdt::SignerSetSnapshot{ signer_set_, 2 }; },
                    []() -> std::shared_ptr<sgns::securecrdt::ISignedCRDTData>
                    { return std::make_shared<TestSignedData>(); },
                    std::regex(),
                    this } ) );
        }

        void TearDown() override
        {
            secure_crdt_->Registry().UnregisterIf( BASE_KEY, this );
            secure_crdt_.reset();
            node_.reset();
            GeniusAccount::SetSecureStorageFactory( nullptr );
            boost::filesystem::remove_all( path_ );
        }

        sgns::crdt::HierarchicalKey                   base_key_{ BASE_KEY };
        boost::filesystem::path                       path_;
        std::vector<std::shared_ptr<GeniusAccount>>   signers_;
        std::vector<std::string>                      signer_set_;
        std::unique_ptr<SecureCrdtTestNode>           node_;
        std::shared_ptr<sgns::securecrdt::SecureCrdt> secure_crdt_;
    };
} // namespace sgns::test::securecrdt

#endif // SGNS_TEST_SECURECRDT_QUORUM_FIXTURE_HPP
