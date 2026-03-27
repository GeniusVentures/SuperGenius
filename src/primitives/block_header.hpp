#ifndef SUPERGENIUS_PRIMITIVES_BLOCK_HEADER_HPP
#define SUPERGENIUS_PRIMITIVES_BLOCK_HEADER_HPP

#include <type_traits>

#include <boost/multiprecision/cpp_int.hpp>
#include "base/blob.hpp"
#include "primitives/common.hpp"
#include "primitives/compact_integer.hpp"
#include "primitives/digest.hpp"

namespace sgns::primitives
{
    /**
     * @brief Header of a block in the chain.
     *
     * Contains parent linkage, state roots, and auxiliary digest data used for
     * validation and consensus.
     */
    struct BlockHeader
    {
        BlockHash     parent_hash;     ///< 32-byte Blake2s hash of parent header
        BlockNumber   number = 0U;     ///< index of current block in the chain
        base::Hash256 state_root;      ///< root of the Merkle tree
        base::Hash256 extrinsics_root; ///< field for validation integrity
        Digest        digest;          ///< chain-specific auxiliary data

        bool operator==( const BlockHeader &rhs ) const
        {
            return std::tie( parent_hash, number, state_root, extrinsics_root, digest ) ==
                   std::tie( rhs.parent_hash, rhs.number, rhs.state_root, rhs.extrinsics_root, rhs.digest );
        }

        bool operator!=( const BlockHeader &rhs ) const
        {
            return !operator==( rhs );
        }

        //added to fix link error in test mode
        friend std::ostream &operator<<( std::ostream &out, const BlockHeader &bh )
        {
            out << bh.parent_hash << CompactInteger( bh.number ) << bh.state_root << bh.extrinsics_root;

            out << bh.digest.size();
            for ( const auto &it : bh.digest )
            {
                out << it.which();
            }
            return out;
        }

        //end
    };

    /**
     * @brief Outputs a BlockHeader to an encoding stream.
     * @tparam Stream Output stream type.
     * @param s Stream reference.
     * @param bh Header to output.
     * @return Reference to stream.
     */
    template <class Stream, typename = std::enable_if_t<Stream::is_encoder_stream>>
    Stream &operator<<( Stream &s, const BlockHeader &bh )
    {
        return s << bh.parent_hash << CompactInteger( bh.number ) << bh.state_root << bh.extrinsics_root << bh.digest;
    }

    /**
     * @brief Decodes a BlockHeader from a decoding stream.
     * @tparam Stream Input stream type.
     * @param s Stream reference.
     * @param bh Header to populate.
     * @return Reference to stream.
     */
    template <class Stream, typename = std::enable_if_t<Stream::is_decoder_stream>>
    Stream &operator>>( Stream &s, BlockHeader &bh )
    {
        CompactInteger number_compact;
        s >> bh.parent_hash >> number_compact >> bh.state_root >> bh.extrinsics_root >> bh.digest;
        bh.number = number_compact.convert_to<BlockNumber>();
        return s;
    }
}

#endif
