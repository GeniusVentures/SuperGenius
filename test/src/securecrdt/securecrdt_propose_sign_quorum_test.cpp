/**
 * @file       securecrdt_propose_sign_quorum_test.cpp
 * @brief      SCRDT-04: proves the full propose+sign+quorum sequence, using
 *             only SecureCrdt::ProposeValue/AddSignature/ReadIfQuorum -- no
 *             raw GlobalDB::Put/Get calls appear in this test body.
 */
#include "securecrdt_quorum_fixture.hpp"

namespace
{
    using sgns::test::securecrdt::SecureCrdtQuorumFixture;
}

TEST_F( SecureCrdtQuorumFixture, FullProposeSignQuorumSequenceReturnsValueOnlyAtThreshold )
{
    const std::vector<uint8_t> payload = { 'p', 'a', 'y', 'l', 'o', 'a', 'd' };

    auto propose_result = secure_crdt_->ProposeValue( base_key_, payload );
    ASSERT_FALSE( propose_result.has_error() ) << propose_result.error().message();

    auto sig1 = signers_[0]->Sign( payload );
    auto add1 = secure_crdt_->AddSignature( base_key_, signers_[0]->GetAddress(), sig1 );
    ASSERT_FALSE( add1.has_error() ) << add1.error().message();

    auto read_after_one = secure_crdt_->ReadIfQuorum( base_key_ );
    ASSERT_FALSE( read_after_one.has_error() );
    EXPECT_FALSE( read_after_one.value().has_value() ) << "1 signature < required 2 must not meet quorum";

    auto sig2 = signers_[1]->Sign( payload );
    auto add2 = secure_crdt_->AddSignature( base_key_, signers_[1]->GetAddress(), sig2 );
    ASSERT_FALSE( add2.has_error() ) << add2.error().message();

    auto read_after_two = secure_crdt_->ReadIfQuorum( base_key_ );
    ASSERT_FALSE( read_after_two.has_error() );
    ASSERT_TRUE( read_after_two.value().has_value() ) << "2 signatures satisfy the required 2";
    EXPECT_EQ( read_after_two.value()->toVector(), payload );
}
