#ifndef SUPERGENIUS_NODE_DAEMON_CONFIG_HPP
#define SUPERGENIUS_NODE_DAEMON_CONFIG_HPP

#include "lib/errors.hpp"
// #include "node_pow_server_config.hpp"
// #include "node_rpc_config.hpp"
#include "node_config.hpp"
// #include "openclconfig.hpp"

#include <vector>

namespace sgns
{
class jsonconfig;
class tomlconfig;
class daemon_config
{
public:
	daemon_config () = default;
	daemon_config (boost::filesystem::path const & data_path);
	::sgns::error_ deserialize_json (bool &, sgns::jsonconfig &);
	::sgns::error_ serialize_json (sgns::jsonconfig &);
	::sgns::error_ deserialize_toml (sgns::tomlconfig &);
	::sgns::error_ serialize_toml (sgns::tomlconfig &);
	// bool rpc_enable{ false };
	// sgns::node_rpc_config rpc;
	::sgns::node_config node;
	bool opencl_enable{ false };
	// sgns::opencl_config opencl;
	// sgns::node_pow_server_config pow_server;
	boost::filesystem::path data_path;
	unsigned json_version () const
	{
		return 2;
	}
};

::sgns::error_ read_node_config_toml (boost::filesystem::path const &, sgns::daemon_config & config_a, std::vector<std::string> const & config_overrides = std::vector<std::string> ());
::sgns::error_ read_and_update_daemon_config (boost::filesystem::path const &, sgns::daemon_config & config_a, sgns::jsonconfig & json_a);
}

#endif