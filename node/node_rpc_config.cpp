#include <lib/config.hpp>
#include <lib/jsonconfig.hpp>
#include <lib/tomlconfig.hpp>
#include "node_rpc_config.hpp"



void sgns::node_rpc_config::migrate (sgns::jsonconfig & json, boost::filesystem::path const & data_path)
{
	sgns::jsonconfig rpc_json;
	auto rpc_config_path = sgns::get_rpc_config_path (data_path);
	auto rpc_error (rpc_json.read (rpc_config_path));
	if (rpc_error || rpc_json.empty ())
	{
		// Migrate RPC info across
		json.write (rpc_config_path);
	}
}
