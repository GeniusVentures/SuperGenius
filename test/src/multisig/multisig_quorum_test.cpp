#include <gtest/gtest.h>

#include <boost/filesystem/operations.hpp>

#include "account/GeniusAccount.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "multisig/MultiSig.hpp"

namespace
{
    using namespace sgns;

    // Five distinct deterministic secp256k1 private keys used to build a 5-member signer set.
    constexpr const char *PRIVATE_KEYS[] = {
        "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaa",
        "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaab",
        "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaac",
        "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaad",
        "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaae",
    };
    // A sixth key, deliberately NOT in the signer set, used for the unauthorized-signer test.
    constexpr char OUTSIDER_PRIVATE_KEY[] =
        "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaf";

    class MultiSigQuorumTest : public ::testing::Test
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
            outsider_ = GeniusAccount::NewFromPrivateKey(
                TokenID::FromBytes( { 0x00 } ), OUTSIDER_PRIVATE_KEY, path_ );
        }

        void TearDown() override
        {
            GeniusAccount::SetSecureStorageFactory( nullptr );
            boost::filesystem::remove_all( path_ );
        }

        boost::filesystem::path                            path_;
        std::vector<std::shared_ptr<GeniusAccount>>        signers_;
        std::vector<std::string>                           signer_set_;
        std::shared_ptr<GeniusAccount>                     outsider_;
        const std::vector<uint8_t>                         payload_ = { 'q', 'u', 'o', 'r', 'u', 'm' };
    };
} // namespace

TEST_F( MultiSigQuorumTest, ExactlyThresholdOfFiveHasQuorum )
{
    multisig::CollectedSignatures collected;
    for ( size_t i = 0; i < 3; ++i )
    {
        collected.emplace_back( signers_[i]->GetAddress(), signers_[i]->Sign( payload_ ) );
    }

    const multisig::MultiSig quorum( signer_set_, 3 );
    auto                     result = quorum.EvaluateQuorum( collected, payload_ );
    EXPECT_TRUE( quorum.IsValid() );
    EXPECT_EQ( quorum.RequiredSignatures(), 3u );
    EXPECT_EQ( quorum.AuthorizedSignerCount(), 5u );
    EXPECT_TRUE( result.has_quorum );
    EXPECT_EQ( result.valid_unique_count, 3u );
}

TEST_F( MultiSigQuorumTest, OneBelowThresholdNoQuorum )
{
    multisig::CollectedSignatures collected;
    for ( size_t i = 0; i < 2; ++i )
    {
        collected.emplace_back( signers_[i]->GetAddress(), signers_[i]->Sign( payload_ ) );
    }

    const multisig::MultiSig quorum( signer_set_, 3 );
    auto                     result = quorum.EvaluateQuorum( collected, payload_ );
    EXPECT_FALSE( result.has_quorum );
    EXPECT_EQ( result.valid_unique_count, 2u );
}

TEST_F( MultiSigQuorumTest, AllFiveSignersHasQuorum )
{
    multisig::CollectedSignatures collected;
    for ( auto &signer : signers_ )
    {
        collected.emplace_back( signer->GetAddress(), signer->Sign( payload_ ) );
    }

    const multisig::MultiSig quorum( signer_set_, 3 );
    auto                     result = quorum.EvaluateQuorum( collected, payload_ );
    EXPECT_TRUE( result.has_quorum );
    EXPECT_EQ( result.valid_unique_count, 5u );
}

TEST_F( MultiSigQuorumTest, DuplicateSignerWithGarbageDoesNotFlipQuorum )
{
    multisig::CollectedSignatures collected;
    for ( size_t i = 0; i < 3; ++i )
    {
        collected.emplace_back( signers_[i]->GetAddress(), signers_[i]->Sign( payload_ ) );
    }
    // Same signer as signers_[0], but with a garbage signature this time — dedup must
    // run before verification so this never gets a chance to invalidate the earlier count.
    collected.emplace_back( signers_[0]->GetAddress(), std::vector<uint8_t>( 64, 0 ) );

    const multisig::MultiSig quorum( signer_set_, 3 );
    auto                     result = quorum.EvaluateQuorum( collected, payload_ );
    EXPECT_TRUE( result.has_quorum );
    EXPECT_EQ( result.valid_unique_count, 3u );
}

TEST_F( MultiSigQuorumTest, UnauthorizedSignerNotCounted )
{
    multisig::CollectedSignatures collected;
    for ( size_t i = 0; i < 2; ++i )
    {
        collected.emplace_back( signers_[i]->GetAddress(), signers_[i]->Sign( payload_ ) );
    }
    // Outsider signs validly, but is not in signer_set_ — must not count toward quorum.
    collected.emplace_back( outsider_->GetAddress(), outsider_->Sign( payload_ ) );

    const multisig::MultiSig quorum( signer_set_, 3 );
    auto                     result = quorum.EvaluateQuorum( collected, payload_ );
    EXPECT_FALSE( result.has_quorum );
    EXPECT_EQ( result.valid_unique_count, 2u );
}

TEST_F( MultiSigQuorumTest, ZeroRequiredSignaturesIsInvalidAndFailsClosed )
{
    multisig::CollectedSignatures collected;
    const multisig::MultiSig      quorum( signer_set_, 0 );

    auto result = quorum.EvaluateQuorum( collected, payload_ );
    EXPECT_FALSE( quorum.IsValid() );
    EXPECT_FALSE( result.has_quorum );
    EXPECT_EQ( result.valid_unique_count, 0u );
}

TEST_F( MultiSigQuorumTest, RequiredSignaturesAboveSignerCountIsInvalidAndFailsClosed )
{
    const multisig::MultiSig quorum( signer_set_, 6 );

    EXPECT_FALSE( quorum.IsValid() );
    EXPECT_EQ( quorum.RequiredSignatures(), 6u );
    EXPECT_EQ( quorum.AuthorizedSignerCount(), 5u );
    EXPECT_FALSE( quorum.EvaluateQuorum( {}, payload_ ).has_quorum );
}

TEST_F( MultiSigQuorumTest, DuplicateAuthorizedAddressesCountOnceTowardM )
{
    auto signer_set_with_duplicate = signer_set_;
    signer_set_with_duplicate.push_back( signer_set_.front() );

    const multisig::MultiSig quorum( signer_set_with_duplicate, 5 );

    EXPECT_TRUE( quorum.IsValid() );
    EXPECT_EQ( quorum.AuthorizedSignerCount(), 5u );
}
