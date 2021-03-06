#ifndef SUPERGENIUS_NODE_HPP
#define SUPERGENIUS_NODE_HPP
#include <lib/stats.hpp>
#include <lib/alarm.hpp>
#include <secure/ledger.hpp>
#include <secure/utility.hpp>
#include "node_config.hpp"
#include "wallet.hpp"
#include <boost/program_options.hpp>
namespace sgns
{
class node final : public std::enable_shared_from_this<sgns::node>
{
public:
    node(sgns::alarm & alarm_a, sgns::node_config const & config_a, sgns::node_flags flags_a);
    ~node ();
    void keepalive (std::string const &, uint16_t);
    void start ();
	void stop ();
    std::shared_ptr<sgns::node> shared ();
    int store_version ();
    void receive_confirmed (sgns::transaction const &, std::shared_ptr<sgns::block>, sgns::block_hash const &);
    void process_confirmed_data (sgns::transaction const &, std::shared_ptr<sgns::block>, sgns::block_hash const &, sgns::account &, sgns::uint128_t &, bool &, sgns::account &);
    // void process_confirmed (sgns::election_status const & status_a, std::shared_ptr<sgns::election> const & election_a, uint8_t iteration_a);
    void process_active (std::shared_ptr<sgns::block>);
    sgns::process_return process_local (std::shared_ptr<sgns::block>, bool const = false);
    sgns::block_hash latest (sgns::account const &);
	sgns::uint128_t balance (sgns::account const &);
    sgns::process_return process (sgns::block &);
    std::shared_ptr<sgns::block> block (sgns::block_hash const &);
	std::pair<sgns::uint128_t, sgns::uint128_t> balance_pending (sgns::account const &);
	sgns::keypair node_id;
    std::unique_ptr<sgns::block_store> store_impl;
    sgns::block_store & store;
    sgns::ledger ledger;
    sgns::network_params network_params;
    sgns::stat stats;
    sgns::node_config config;
    sgns::node_flags flags;
    sgns::alarm & alarm;
    std::atomic<bool> stopped{ false };
    // std::unique_ptr<sgns::wallets_store> wallets_store_impl;
    // sgns::wallets_store & wallets_store;
    // sgns::wallets wallets;
private:
    void check_genesis();
};

sgns::node_flags const & inactive_node_flag_defaults ();

class inactive_node final
{
public:
	inactive_node (boost::filesystem::path const & path_a, sgns::node_flags const & node_flags_a = sgns::inactive_node_flag_defaults ());
	~inactive_node ();
	// std::shared_ptr<boost::asio::io_context> io_context;
	// sgns::alarm alarm;
	// sgns::work_pool work;
	std::shared_ptr<sgns::node> node;
};
std::unique_ptr<sgns::inactive_node> default_inactive_node (boost::filesystem::path const &, boost::program_options::variables_map const &);
}
#endif 