#ifndef SUPERGENIUS_NODE_HPP
#define SUPERGENIUS_NODE_HPP
#include <secure/ledger.hpp>
#include <secure/utility.hpp>
namespace sgns
{
class node final : public std::enable_shared_from_this<sgns::node>
{
public:
    node();
    ~node ();
    void keepalive (std::string const &, uint16_t);
    void start ();
	void stop ();
    std::shared_ptr<sgns::node> shared ();
    int store_version ();
    sgns::block_hash latest (sgns::account const &);
	sgns::uint128_t balance (sgns::account const &);
    sgns::process_return process (sgns::block &);
    std::shared_ptr<sgns::block> block (sgns::block_hash const &);
	std::pair<sgns::uint128_t, sgns::uint128_t> balance_pending (sgns::account const &);
	sgns::keypair node_id;
    sgns::ledger ledger;
    sgns::network_params network_params;
};
}
#endif 