#ifndef SGNS_TESTUTIL_GLOBALDB_NETWORK_CONFIG_HPP
#define SGNS_TESTUTIL_GLOBALDB_NETWORK_CONFIG_HPP

#include <fstream>
#include <string>

#include <boost/filesystem.hpp>
#include <gtest/gtest.h>

namespace sgns::test
{
    /// Minimal GlobalDB network config consumed by
    /// GlobalDbNetworkComposition::LoadNetworkConfig (empty bootstrap = none).
    inline void WriteGlobalDbNetworkConfig( const boost::filesystem::path &config_path,
                                            const std::string              &bootstrap_address = {} )
    {
        std::ofstream output( config_path.string() );
        ASSERT_TRUE( output.good() );
        output << R"({"pubsub_port":"0","pubsub_bind_address":"0.0.0.0","high_water":20,"low_water":1,"bootstrap_addresses":[)";
        if ( !bootstrap_address.empty() )
        {
            output << '"' << bootstrap_address << '"';
        }
        output << "]}";
    }
} // namespace sgns::test

#endif // SGNS_TESTUTIL_GLOBALDB_NETWORK_CONFIG_HPP
