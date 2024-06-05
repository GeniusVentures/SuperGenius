#include "blockchain/impl/key_value_block_header_repository.hpp"

#include <boost/optional.hpp>

#include "blockchain/impl/common.hpp"
#include "blockchain/impl/storage_util.hpp"
#include "blockchain/impl/proto/SGBlocks.pb.h"

using sgns::base::Hash256;
using sgns::primitives::BlockId;
using sgns::primitives::BlockNumber;

namespace sgns::blockchain
{

    KeyValueBlockHeaderRepository::KeyValueBlockHeaderRepository( std::shared_ptr<crdt::GlobalDB> db,
                                                                  std::shared_ptr<crypto::Hasher> hasher,
                                                                  const std::string              &net_id ) :
        db_{ std::move( db ) }, hasher_{ std::move( hasher ) }
    {
        BOOST_ASSERT( hasher_ );
        block_header_key_prefix = net_id + std::string( BLOCKCHAIN_PATH );
    }

    outcome::result<BlockNumber> KeyValueBlockHeaderRepository::getNumberByHash( const Hash256 &hash ) const
    {
        OUTCOME_TRY( ( auto &&, key ), idToBufferKey( *db_, hash ) );

        return BufferToNumber( key );
    }

    outcome::result<base::Hash256> KeyValueBlockHeaderRepository::getHashByNumber(
        const primitives::BlockNumber &number ) const
    {
        OUTCOME_TRY( ( auto &&, header ), getBlockHeader( number ) );
        auto serializedHeader = GetHeaderSerializedData( header );

        return hasher_->blake2b_256( serializedHeader );
    }

    outcome::result<primitives::BlockHeader> KeyValueBlockHeaderRepository::getBlockHeader( const BlockId &id ) const
    {
        OUTCOME_TRY( ( auto &&, header_string_val ), idToStringKey( *db_, id ) );

        auto header_res = db_->Get( { block_header_key_prefix + header_string_val } );
        if ( !header_res )
        {
            return ( isNotFoundError( header_res.error() ) ) ? Error::BLOCK_NOT_FOUND : header_res.error();
        }

        return GetBlockHeaderFromSerialized( header_res.value().toVector() );
    }

    outcome::result<primitives::BlockHash> KeyValueBlockHeaderRepository::putBlockHeader(
        const primitives::BlockHeader &header )
    {
        auto encoded_header = GetHeaderSerializedData( header );
        auto header_hash    = hasher_->blake2b_256( encoded_header );

        OUTCOME_TRY( ( auto &&, id_string ), idToStringKey( *db_, header.number ) );
        BOOST_OUTCOME_TRYV2( auto &&, db_->Put( { header_hash.toReadableString() }, NumberToBuffer( header.number ) ) );
        BOOST_OUTCOME_TRYV2(
            auto &&, db_->Put( { block_header_key_prefix + id_string }, base::Buffer{ std::move( encoded_header ) } ) );

        return header_hash;
    }

    outcome::result<void> KeyValueBlockHeaderRepository::removeBlockHeader( const BlockId &id )
    {
        OUTCOME_TRY( ( auto &&, header_string_val ), idToStringKey( *db_, id ) );

        return db_->Remove( { block_header_key_prefix + header_string_val } );
    }

    outcome::result<BlockStatus> KeyValueBlockHeaderRepository::getBlockStatus( const primitives::BlockId &id ) const
    {
        return getBlockHeader( id ).has_value() ? BlockStatus::InChain : BlockStatus::Unknown;
    }

    std::string KeyValueBlockHeaderRepository::GetHeaderPath() const
    {
        return block_header_key_prefix;
    }

    std::vector<uint8_t> KeyValueBlockHeaderRepository::GetHeaderSerializedData( const primitives::BlockHeader &header )
    {
        SGBlocks::BlockHeaderData header_proto;
        header_proto.set_parent_hash( header.parent_hash.toReadableString() );
        header_proto.set_block_number( header.number );
        header_proto.set_state_root( header.state_root.toReadableString() );
        header_proto.set_extrinsics_root( header.extrinsics_root.toReadableString() );

        size_t               size = header_proto.ByteSizeLong();
        std::vector<uint8_t> serialized_proto( size );

        header_proto.SerializeToArray( serialized_proto.data(), static_cast<int>( serialized_proto.size() ) );

        return serialized_proto;
    }

    primitives::BlockHeader KeyValueBlockHeaderRepository::GetBlockHeaderFromSerialized(
        const std::vector<uint8_t> &serialized_data )
    {
        SGBlocks::BlockHeaderData header_proto;

        if ( !header_proto.ParseFromArray( serialized_data.data(), static_cast<int>( serialized_data.size() ) ) )
        {
            std::cerr << "Failed to parse BlockHeaderData from array." << std::endl;
        }

        primitives::BlockHeader block_header;
        block_header.parent_hash     = ( Hash256::fromReadableString( header_proto.parent_hash() ) ).value();
        block_header.number          = header_proto.block_number();
        block_header.state_root      = ( Hash256::fromReadableString( header_proto.state_root() ) ).value();
        block_header.extrinsics_root = ( Hash256::fromReadableString( header_proto.extrinsics_root() ) ).value();

        return block_header;
    }
}
