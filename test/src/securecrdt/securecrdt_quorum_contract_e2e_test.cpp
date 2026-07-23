/**
 * @file       securecrdt_quorum_contract_e2e_test.cpp
 * @brief      Warning 2 closure: proves ReadIfQuorum's documented handoff
 *             contract end-to-end -- propose, sign to quorum, ReadIfQuorum,
 *             then DeserializeFromBytes+Verify+Apply on the returned bytes
 *             exactly as a real downstream consumer would.
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
        "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05ead0",
        "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05ead1",
    };

    std::string SignatureAsString( const std::vector<uint8_t> &signature )
    {
        return std::string( signature.begin(), signature.end() );
    }

    /// @brief Test-local ISignedCRDTData implementer with a visible side
    ///        effect flag set only by Apply() -- proves the full downstream
    ///        DeserializeFromBytes/Verify/Apply chain, not just that
    ///        SecureCrdt returns bytes.
    class TestSignedDataE2E : public ISignedCRDTData
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

    class SecureCrdtQuorumContractE2ETest : public ::testing::Test
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

            node_ = sgns::test::securecrdt::MakeSecureCrdtTestNode( "securecrdt_quorum_contract_e2e" );
            ASSERT_NE( node_, nullptr );

            SecureCrdtRegistry::Register( "gnus-test-quorum-contract-e2e",
                                          SecureCrdtRegistryEntry{
                                              "gnus-test-quorum-contract-e2e",
                                              []( const std::string & ) -> outcome::result<SignerSetSnapshot>
                                              { return SignerSetSnapshot{ signer_set_static(), 2 }; },
                                              []() -> std::shared_ptr<ISignedCRDTData>
                                              { return std::make_shared<TestSignedDataE2E>(); },
                                              std::regex(), &token_ } );

            secure_crdt_ = std::make_shared<SecureCrdt>( node_->db, "securecrdt_test_topic" );
        }

        void TearDown() override
        {
            SecureCrdtRegistry::UnregisterIf( "gnus-test-quorum-contract-e2e", &token_ );
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

TEST_F( SecureCrdtQuorumContractE2ETest, ReadIfQuorumHandoffContractWorksEndToEnd )
{
    const sgns::crdt::HierarchicalKey base_key( "gnus-test-quorum-contract-e2e" );
    const std::vector<uint8_t>        payload = { 'e', '2', 'e', '-', 'p', 'a', 'y', 'l', 'o', 'a', 'd' };

    auto propose_result = secure_crdt_->ProposeValue( base_key, payload );
    ASSERT_FALSE( propose_result.has_error() ) << propose_result.error().message();

    auto sig1 = SignatureAsString( signers_[0]->Sign( payload ) );
    ASSERT_FALSE( secure_crdt_->AddSignature( base_key, signers_[0]->GetAddress(), sig1 ).has_error() );

    auto sig2 = SignatureAsString( signers_[1]->Sign( payload ) );
    ASSERT_FALSE( secure_crdt_->AddSignature( base_key, signers_[1]->GetAddress(), sig2 ).has_error() );

    auto read_result = secure_crdt_->ReadIfQuorum( base_key );
    ASSERT_FALSE( read_result.has_error() );
    ASSERT_TRUE( read_result.value().has_value() ) << "quorum should be met after 2/2 valid signatures";

    // Full downstream handoff chain per ReadIfQuorum's documented @note contract:
    TestSignedDataE2E downstream_consumer;
    ASSERT_TRUE( downstream_consumer.DeserializeFromBytes( read_result.value()->toVector() ) )
        << "well-formed payload must deserialize cleanly";
    ASSERT_TRUE( downstream_consumer.Verify( read_result.value()->toVector() ) );
    EXPECT_FALSE( downstream_consumer.applied_ );
    downstream_consumer.Apply();
    EXPECT_TRUE( downstream_consumer.applied_ ) << "Apply()'s side effect must run after quorum is confirmed";
}
