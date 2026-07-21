#ifndef SGNS_TESTUTIL_GENIUS_NODE_TEST_ACCESS_HPP
#define SGNS_TESTUTIL_GENIUS_NODE_TEST_ACCESS_HPP

#include <chrono>
#include <memory>

#include "account/GeniusNode.hpp"

namespace sgns
{
    class GeniusNodeTestAccess
    {
    public:
        static void CacheGnusPrice( const std::shared_ptr<GeniusNode> &node, double price )
        {
            if ( node )
            {
                node->m_tokenPriceCache["genius-ai"] = { price, std::chrono::system_clock::now() };
            }
        }
    };
} // namespace sgns

#endif // SGNS_TESTUTIL_GENIUS_NODE_TEST_ACCESS_HPP
