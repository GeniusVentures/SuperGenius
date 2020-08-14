#ifndef SUPERGENIUS_NODE_CLI_HPP
#define SUPERGENIUS_NODE_CLI_HPP
#include <lib/errors.hpp>
#include "node_config.hpp"

#include <boost/program_options.hpp>

namespace sgns
{
/** Command line related error codes */
enum class error_cli
{
	generic = 1,
	parse_error = 2,
	invalid_arguments = 3,
	unknown_command = 4,
	database_write_error = 5,
	reading_config = 6,
	disable_all_network = 7,
	ambiguous_udp_options = 8
};

void add_node_options (boost::program_options::options_description &);
void add_node_flag_options (boost::program_options::options_description &);
std::error_code update_flags (sgns::node_flags &, boost::program_options::variables_map const &);
std::error_code handle_node_options (boost::program_options::variables_map const &);
}

REGISTER_ERROR_CODES (sgns, error_cli)
#endif