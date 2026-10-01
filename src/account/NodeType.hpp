/**
 * @file       NodeType.hpp
 * @brief      Deployment node role shared across the node, transaction, and migration layers.
 * @date       2026-08-17
 *
 * Lives in its own header (rather than nested in GeniusNode) so that lower layers —
 * TransactionManager, MigrationManager — can take a NodeType without including the
 * GeniusNode facade that owns them.
 */
#ifndef _SGNS_NODE_TYPE_HPP_
#define _SGNS_NODE_TYPE_HPP_

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <fmt/format.h>

namespace sgns
{
    /**
     * @brief Deployment node role, read from sgns_config.json ("node_type").
     *
     * - @c Full    participates in everything: replicates all accounts, proposes, votes, mirrors results.
     * - @c Light   replicates only its own account's data.
     * - @c Archive replicates everything like Full, but performs no consensus or result work.
     *              It is a passive replica: it stores, it does not act.
     */
    enum class NodeType : uint8_t
    {
        Full    = 0, ///< Full node: replicates everything and participates in consensus.
        Light   = 1, ///< Light node: own account only. Default on missing/unknown key.
        Archive = 2, ///< Archive node: replicates everything, participates in nothing.
    };

    /**
     * @brief Whether this role replicates network-wide data rather than just its own account.
     * @return True for Full and Archive, false for Light.
     *
     * Gates the full-node topic subscription, network-wide UTXO roots, and full-node
     * blockchain mode — i.e. everything on the "store it" side of the split.
     */
    constexpr bool ReplicatesAllAccounts( NodeType type ) noexcept
    {
        return type != NodeType::Light;
    }

    /**
     * @brief Whether this role actively participates in consensus and result handling.
     * @return True for Full and Light, false for Archive.
     *
     * Archive is a passive replica, so it declines the work-generating side of full-node
     * behavior (proposals, certificate authorship, result mirroring) while keeping replication.
     */
    constexpr bool ParticipatesInConsensus( NodeType type ) noexcept
    {
        return type != NodeType::Archive;
    }

    /**
     * @brief Canonical lowercase name of a role, as accepted in sgns_config.json.
     */
    constexpr std::string_view NodeTypeToString( NodeType type ) noexcept
    {
        switch ( type )
        {
            case NodeType::Full:
                return "full";
            case NodeType::Light:
                return "light";
            case NodeType::Archive:
                return "archive";
        }
        return "light";
    }

    /**
     * @brief Parses a node role name, case-insensitively (Phase-2 D-02).
     * @return The matching role, or std::nullopt if the string is not a known role.
     */
    inline std::optional<NodeType> NodeTypeFromString( std::string_view s )
    {
        std::string lower;
        lower.reserve( s.size() );
        for ( char c : s )
        {
            lower.push_back( static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) ) );
        }
        if ( lower == "full" )
        {
            return NodeType::Full;
        }
        if ( lower == "light" )
        {
            return NodeType::Light;
        }
        if ( lower == "archive" )
        {
            return NodeType::Archive;
        }
        return std::nullopt;
    }
}

/// Lets a NodeType be passed straight to any spdlog/fmt call: `logger->info( "role={}", node_type_ )`.
template <>
struct fmt::formatter<sgns::NodeType> : formatter<std::string_view>
{
    format_context::iterator format( sgns::NodeType type, format_context &ctx ) const
    {
        return formatter<string_view>::format( sgns::NodeTypeToString( type ), ctx );
    }
};

#endif
