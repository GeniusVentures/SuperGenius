/**
 * @file       securecrdt_quorum_contract_e2e_test.cpp
 * @brief      Proves ReadIfQuorum's downstream Deserialize/Verify/Apply handoff.
 */
#include "securecrdt_quorum_fixture.hpp"

namespace
{
    using sgns::test::securecrdt::SecureCrdtQuorumFixture;
    using sgns::test::securecrdt::TestSignedData;
}

TEST_F( SecureCrdtQuorumFixture, ReadIfQuorumHandoffContractWorksEndToEnd )
{
    const std::vector<uint8_t> payload = { 'e', '2', 'e', '-', 'p', 'a', 'y', 'l', 'o', 'a', 'd' };

    auto propose_result = secure_crdt_->ProposeValue( base_key_, payload );
    ASSERT_FALSE( propose_result.has_error() ) << propose_result.error().message();

    auto sig1 = signers_[0]->Sign( payload );
    ASSERT_FALSE( secure_crdt_->AddSignature( base_key_, signers_[0]->GetAddress(), sig1 ).has_error() );

    auto sig2 = signers_[1]->Sign( payload );
    ASSERT_FALSE( secure_crdt_->AddSignature( base_key_, signers_[1]->GetAddress(), sig2 ).has_error() );

    auto read_result = secure_crdt_->ReadIfQuorum( base_key_ );
    ASSERT_FALSE( read_result.has_error() );
    ASSERT_TRUE( read_result.value().has_value() ) << "quorum should be met after 2/2 valid signatures";

    TestSignedData downstream_consumer;
    ASSERT_TRUE( downstream_consumer.DeserializeFromBytes( read_result.value()->toVector() ) )
        << "well-formed payload must deserialize cleanly";
    ASSERT_TRUE( downstream_consumer.Verify( read_result.value()->toVector() ) );
    EXPECT_FALSE( downstream_consumer.applied_ );
    downstream_consumer.Apply();
    EXPECT_TRUE( downstream_consumer.applied_ ) << "Apply() must run after quorum is confirmed";
}
