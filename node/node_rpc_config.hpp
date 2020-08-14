#ifndef SUPERGENIUS_NODE_RPC_CONFIG_HPP
#define SUPERGENIUS_NODE_RPC_CONFIG_HPP

#include <lib/errors.hpp>
#include <lib/jsonconfig.hpp>
#include <string>

namespace boost
{
namespace filesystem
{
	class path;
}
}

namespace sgns
{
class tomlconfig;
// class rpc_child_process_config final
// {
// public:
// 	bool enable{ false };
// 	std::string rpc_path{ get_default_rpc_filepath () };
// };

class node_rpc_config final
{
public:


	bool enable_sign_hash{ false };
	// sgns::rpc_child_process_config child_process;
	static unsigned json_version ()
	{
		return 1;
	}

private:
	void migrate (sgns::jsonconfig & json, boost::filesystem::path const & data_path);
};
}
#endif