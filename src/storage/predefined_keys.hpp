

#ifndef SUPERGENIUS_SRC_STORAGE_PREDEFINED_KEYS_HPP
#define SUPERGENIUS_SRC_STORAGE_PREDEFINED_KEYS_HPP

#include <string>

namespace sgns::storage
{
    const std::string &GetAuthoritySetKey()
    {
        static const std::string kAuthoritySetKey = "finality_voters";
        return kAuthoritySetKey;
    }

    const std::string &GetSetStateKey()
    {
        static const std::string kSetStateKey = "finality_completed_round";
        return kSetStateKey;
    }

    const std::string &GetGenesisBlockHashLookupKey()
    {
        static const std::string kGenesisBlockHashLookupKey = ":sgns:genesis_block_hash";
        return kGenesisBlockHashLookupKey;
    }

    const std::string &GetLastFinalizedBlockHashLookupKey()
    {
        static const std::string kLastFinalizedBlockHashLookupKey = ":sgns:last_finalized_block_hash";
        return kLastFinalizedBlockHashLookupKey;
    }

} // namespace sgns::storage

#endif // SUPERGENIUS_SRC_STORAGE_PREDEFINED_KEYS_HPP
