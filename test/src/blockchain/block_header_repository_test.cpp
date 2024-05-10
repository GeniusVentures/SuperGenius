#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include "blockchain/block_header_repository.hpp"
#include "blockchain/impl/key_value_block_header_repository.hpp"
#include "crypto/hasher/hasher_impl.hpp"
#include "primitives/block_header.hpp"
#include "scale/scale.hpp"
#include "testutil/literals.hpp"
#include "testutil/outcome.hpp"
#include "testutil/storage/base_crdt_test.hpp"

using sgns::base::Hash256;
using sgns::blockchain::BlockHeaderRepository;
using sgns::blockchain::KeyValueBlockHeaderRepository;
using sgns::primitives::BlockHeader;
using sgns::primitives::BlockId;
using sgns::primitives::BlockNumber;

static BlockHeader defaultHeader( BlockNumber number )
{
    BlockHeader header;

    header.parent_hash     = "ABCDEF"_hash256;
    header.number          = number;
    header.state_root      = "010203"_hash256;
    header.extrinsics_root = "DEADBEEF"_hash256;

    return header;
}

static BlockHeader defaultHeader()
{
    return defaultHeader( 42 );
}

class BlockHeaderRepositoryFixture : public test::CRDTFixture
{
public:
    BlockHeaderRepositoryFixture() :
        CRDTFixture( fs::path( "blockheaderrepotest.lvldb" ) ), hasher( std::make_shared<sgns::crypto::HasherImpl>() ),
        header_repo_( std::make_shared<KeyValueBlockHeaderRepository>( db_, hasher, "testheader-963/" ) )
    {
    }

    std::shared_ptr<sgns::crypto::Hasher>  hasher;
    std::shared_ptr<BlockHeaderRepository> header_repo_;
};

class BlockHeaderRepositoryNumberParametrizedFixture : public BlockHeaderRepositoryFixture,
                                                       public testing::WithParamInterface<BlockNumber>
{
};

const std::vector<BlockNumber> ParamValues = { 1, 42, 12345, 0, 0xFFFFFFFF };

/**
 * @given HeaderBackend instance
 * @when learning block hash by its number through HeaderBackend
 * @then resulting hash is equal to the original hash of the block for both
 * retrieval through getHashByNumber and getHashById
 */
TEST_P( BlockHeaderRepositoryNumberParametrizedFixture, GetHashByNumber )
{
    auto number = GetParam();

    auto header = defaultHeader( number );

    EXPECT_OUTCOME_TRUE( hash, header_repo_->putBlockHeader( header ) );

    EXPECT_OUTCOME_TRUE( maybe_hash, header_repo_->getHashByNumber( number ) );
    ASSERT_THAT( hash, testing::ContainerEq( maybe_hash ) );

    EXPECT_OUTCOME_TRUE( maybe_another_hash, header_repo_->getHashById( number ) );
    ASSERT_THAT( hash, testing::ContainerEq( maybe_another_hash ) );
}

/**
 * @given HeaderBackend instance
 * @when learning block number by its hash through HeaderBackend
 * @then resulting number is equal to the original block number for both
 * retrieval through getNumberByHash and getNumberById
 */
TEST_P( BlockHeaderRepositoryNumberParametrizedFixture, GetNumberByHash )
{
    auto header = defaultHeader( GetParam() );

    EXPECT_OUTCOME_TRUE( hash, header_repo_->putBlockHeader( header ) );
    EXPECT_OUTCOME_TRUE( maybe_number, header_repo_->getNumberByHash( hash ) );
    ASSERT_EQ( GetParam(), maybe_number );

    EXPECT_OUTCOME_TRUE( maybe_another_number, header_repo_->getNumberById( GetParam() ) );
    ASSERT_EQ( GetParam(), maybe_another_number );
}

/**
 * @given HeaderBackend instance
 * @when retrieving a block header by its id
 * @then the same header that was put into the storage is returned, regardless
 * of whether the id contained a number or a hash
 */
TEST_P( BlockHeaderRepositoryNumberParametrizedFixture, GetHeader )
{
    auto header = defaultHeader( GetParam() );

    EXPECT_OUTCOME_TRUE( hash, header_repo_->putBlockHeader( header ) );

    EXPECT_OUTCOME_TRUE( header_by_num, header_repo_->getBlockHeader( GetParam() ) );
    EXPECT_OUTCOME_TRUE( header_by_hash, header_repo_->getBlockHeader( hash ) );

    ASSERT_EQ( header_by_hash, header );
    ASSERT_EQ( header_by_num, header );
}

INSTANTIATE_TEST_SUITE_P( Numbers, BlockHeaderRepositoryNumberParametrizedFixture, testing::ValuesIn( ParamValues ) );

/**
 * @given HeaderBackend instance with several headers in the storage
 * @when accessing a header that wasn't put into storage
 * @then result is error
 */
TEST_F( BlockHeaderRepositoryFixture, UnexistingHeader )
{
    auto block_number = ParamValues[0];

    for ( auto number : ParamValues )
    {
        if ( number != block_number )
        {
            EXPECT_OUTCOME_TRUE_1( header_repo_->putBlockHeader( defaultHeader( number ) ) );
        }
    }

    BlockHeader not_in_storage = defaultHeader( block_number );

    EXPECT_OUTCOME_TRUE( enc_header, sgns::scale::encode( not_in_storage ) );
    auto hash = hasher->blake2b_256( enc_header );

    EXPECT_OUTCOME_FALSE_1( header_repo_->getBlockHeader( block_number ) );
    EXPECT_OUTCOME_FALSE_1( header_repo_->getBlockHeader( hash ) );

    EXPECT_OUTCOME_FALSE_1( header_repo_->getHashById( block_number ) );
    EXPECT_OUTCOME_FALSE_1( header_repo_->getNumberById( hash ) );

    // These methods work because they don't have to access the storage, they just return the params.
    // This happens because id is a variant of both hash and number.
    EXPECT_OUTCOME_TRUE_1( header_repo_->getHashById( hash ) );
    EXPECT_OUTCOME_TRUE_1( header_repo_->getNumberById( block_number ) );
}
