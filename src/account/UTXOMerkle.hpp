/**
 * @file       UTXOMerkle.hpp
 * @brief      Helpers for deterministic serialization and Merkle hashing of UTXO snapshots.
 * @date       2026-03-18
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef SGNS_UTXO_MERKLE_HPP
#define SGNS_UTXO_MERKLE_HPP

#include "account/GeniusUTXO.hpp"
#include "crypto/sha/sha256.hpp"

#include <algorithm>
#include <string>
#include <vector>

/**
 * @brief Utilities for building deterministic Merkle roots over ordered UTXO payloads.
 */
namespace sgns::utxo_merkle
{
    /// Domain separator prefix used for hashed leaf payloads.
    constexpr uint8_t kLeafPrefix = 0x00;
    /// Domain separator prefix used for hashed internal nodes.
    constexpr uint8_t kNodePrefix = 0x01;

    /**
     * @brief       Appends a 32-bit unsigned integer in big-endian order.
     * @param[out]  out The vector to append to
     * @param[in]   value the value to append
     */
    inline void AppendUInt32BE( std::vector<uint8_t> &out, uint32_t value )
    {
        out.push_back( static_cast<uint8_t>( ( value >> 24 ) & 0xFF ) );
        out.push_back( static_cast<uint8_t>( ( value >> 16 ) & 0xFF ) );
        out.push_back( static_cast<uint8_t>( ( value >> 8 ) & 0xFF ) );
        out.push_back( static_cast<uint8_t>( value & 0xFF ) );
    }

    /**
     * @brief       Appends a 64-bit unsigned integer in big-endian order.
     * @param[out]  out The vector to append to 
     * @param[in]   value the value to append
     */
    inline void AppendUInt64BE( std::vector<uint8_t> &out, uint64_t value )
    {
        out.push_back( static_cast<uint8_t>( ( value >> 56 ) & 0xFF ) );
        out.push_back( static_cast<uint8_t>( ( value >> 48 ) & 0xFF ) );
        out.push_back( static_cast<uint8_t>( ( value >> 40 ) & 0xFF ) );
        out.push_back( static_cast<uint8_t>( ( value >> 32 ) & 0xFF ) );
        out.push_back( static_cast<uint8_t>( ( value >> 24 ) & 0xFF ) );
        out.push_back( static_cast<uint8_t>( ( value >> 16 ) & 0xFF ) );
        out.push_back( static_cast<uint8_t>( ( value >> 8 ) & 0xFF ) );
        out.push_back( static_cast<uint8_t>( value & 0xFF ) );
    }

    /**
     * @brief       Reads a 32-bit unsigned integer from big-endian bytes.
     * @param[in]   data A pointer to the byte array
     * @return      the 32 bit unsigned integer represented by the bytes
     */
    inline uint32_t ReadUInt32BE( const uint8_t *data )
    {
        return ( static_cast<uint32_t>( data[0] ) << 24 ) | ( static_cast<uint32_t>( data[1] ) << 16 ) |
               ( static_cast<uint32_t>( data[2] ) << 8 ) | static_cast<uint32_t>( data[3] );
    }

    /**
     * @brief       Reads a 64-bit unsigned integer from big-endian bytes.
     * @param[in]   data A pointer to the byte array
     * @return      the 64 bit unsigned integer represented by the bytes
     */
    inline uint64_t ReadUInt64BE( const uint8_t *data )
    {
        return ( static_cast<uint64_t>( data[0] ) << 56 ) | ( static_cast<uint64_t>( data[1] ) << 48 ) |
               ( static_cast<uint64_t>( data[2] ) << 40 ) | ( static_cast<uint64_t>( data[3] ) << 32 ) |
               ( static_cast<uint64_t>( data[4] ) << 24 ) | ( static_cast<uint64_t>( data[5] ) << 16 ) |
               ( static_cast<uint64_t>( data[6] ) << 8 ) | static_cast<uint64_t>( data[7] );
    }

    /**
     * @brief       Generates a canonical key for a UTXO outpoint, used for deterministic ordering in Merkle tree construction.
     * @param[in]   txid The transaction hash that created the UTXO
     * @param[in]   idx The output index of the UTXO within the transaction
     * @return      Canonical string key in the format "txid:idx" where txid is the readable hex representation of the transaction hash
     */
    inline std::string OutPointKey( const base::Hash256 &txid, uint32_t idx )
    {
        return txid.toReadableString() + ":" + std::to_string( idx );
    }

    /**
     * @brief       Serializes a UTXO into the canonical leaf payload used for Merkle hashing.
     * @param[in]   utxo The UTXO to serialize
     * @return      The serialized leaf payload
     */
    inline std::vector<uint8_t> SerializeUTXOLeafPayload( const GeniusUTXO &utxo )
    {
        std::vector<uint8_t> payload;
        const auto          &owner_address = utxo.GetOwnerAddress();
        const auto           txid          = utxo.GetTxID();
        const auto           token_id      = utxo.GetTokenID();
        const auto          &token_bytes   = token_id.bytes();
        payload.reserve( 32 + 4 + 4 + owner_address.size() + token_bytes.size() + 8 );

        payload.insert( payload.end(), txid.begin(), txid.end() );
        AppendUInt32BE( payload, utxo.GetOutputIdx() );
        AppendUInt32BE( payload, static_cast<uint32_t>( owner_address.size() ) );
        payload.insert( payload.end(), owner_address.begin(), owner_address.end() );
        payload.insert( payload.end(), token_bytes.begin(), token_bytes.end() );
        AppendUInt64BE( payload, utxo.GetAmount() );
        return payload;
    }

    /**
     * @brief       Hashes a serialized UTXO leaf payload with the leaf domain separator.
     * @param[in]   payload The payload to hash
     * @return      The hash of the payload as a leaf node in the Merkle tree
     */
    inline base::Hash256 HashLeaf( const std::vector<uint8_t> &payload )
    {
        std::vector<uint8_t> bytes;
        bytes.reserve( payload.size() + 1 );
        bytes.push_back( kLeafPrefix );
        bytes.insert( bytes.end(), payload.begin(), payload.end() );
        return crypto::sha256( gsl::span<const uint8_t>( bytes.data(), bytes.size() ) );
    }

    /**
     * @brief       Hashes two child nodes with the internal-node domain separator.
     * @param[in]   left  The hash of the left child node
     * @param[in]   right The hash of the right child node
     * @return      The hash of the parent node
     */
    inline base::Hash256 HashNode( const base::Hash256 &left, const base::Hash256 &right )
    {
        std::vector<uint8_t> bytes;
        bytes.reserve( 1 + left.size() + right.size() );
        bytes.push_back( kNodePrefix );
        bytes.insert( bytes.end(), left.begin(), left.end() );
        bytes.insert( bytes.end(), right.begin(), right.end() );
        return crypto::sha256( gsl::span<const uint8_t>( bytes.data(), bytes.size() ) );
    }

    /**
     * @brief       Returns the canonical root used for an empty UTXO set.
     * @return      The canonical root for an empty UTXO set
     */
    inline base::Hash256 EmptyUTXOMerkleRoot()
    {
        static const base::Hash256 empty_root = crypto::sha256( std::string_view( "UTXO_EMPTY_V1" ) );
        return empty_root;
    }

    /**
     * @brief       Reduces a list of leaf hashes into a single Merkle root.
     * @param[in]   level_hashes The list of leaf hashes
     * @return      The computed Merkle root
     */
    inline base::Hash256 ComputeMerkleRootFromLeafHashes( std::vector<base::Hash256> level_hashes )
    {
        if ( level_hashes.empty() )
        {
            return EmptyUTXOMerkleRoot();
        }

        while ( level_hashes.size() > 1 )
        {
            if ( ( level_hashes.size() % 2 ) != 0 )
            {
                level_hashes.push_back( level_hashes.back() );
            }

            std::vector<base::Hash256> next_level;
            next_level.reserve( level_hashes.size() / 2 );
            for ( size_t i = 0; i < level_hashes.size(); i += 2 )
            {
                next_level.push_back( HashNode( level_hashes[i], level_hashes[i + 1] ) );
            }
            level_hashes = std::move( next_level );
        }

        return level_hashes.front();
    }

    /**
     * @brief       Sorts canonical payloads, hashes them as leaves, and computes the Merkle root.
     * @param[in]   payloads The list of serialized UTXO payloads to include in the Merkle tree
     * @return      The computed Merkle root of the payloads
     */
    inline base::Hash256 ComputeMerkleRootFromPayloads( std::vector<std::vector<uint8_t>> payloads )
    {
        if ( payloads.empty() )
        {
            return EmptyUTXOMerkleRoot();
        }

        std::sort( payloads.begin(), payloads.end() );

        std::vector<base::Hash256> leaf_hashes;
        leaf_hashes.reserve( payloads.size() );
        for ( const auto &payload : payloads )
        {
            leaf_hashes.push_back( HashLeaf( payload ) );
        }

        return ComputeMerkleRootFromLeafHashes( std::move( leaf_hashes ) );
    }

    /**
     * @brief       Computes the Merkle root for a given set of UTXOs by serializing them into canonical payloads and hashing them.
     * @param[in]   utxos The list of UTXOs to include in the Merkle tree
     * @return      The computed Merkle root of the UTXOs
     */
    inline base::Hash256 ComputeMerkleRootFromUTXOs( const std::vector<GeniusUTXO> &utxos )
    {
        if ( utxos.empty() )
        {
            return EmptyUTXOMerkleRoot();
        }
        std::vector<std::vector<uint8_t>> payloads;
        payloads.reserve( utxos.size() );
        for ( const auto &utxo : utxos )
        {
            payloads.push_back( SerializeUTXOLeafPayload( utxo ) );
        }
        return ComputeMerkleRootFromPayloads( std::move( payloads ) );
    }
}
#endif // SGNS_UTXO_MERKLE_HPP
