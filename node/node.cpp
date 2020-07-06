#include "node.hpp"
#include "node.hpp"
#include "ipfs_lite_store.hpp"
sgns::node::node (sgns::alarm & alarm_a, sgns::node_config const & config_a， sgns::node_flags flags_a) :
config (config_a),
flags (flags_a),
alarm (alarm_a),
store_impl (sgns::make_store ()),
store (*store_impl),
ledger (store, stats, flags_a.generate_cache, [this]() { this->network.erase_below_version (network_params.protocol.protocol_version_min (true)); })
{

}

std::shared_ptr<sgns::node> sgns::node::shared ()
{
	return shared_from_this ();
}
int sgns::node::store_version ()
{
	auto transaction (store.tx_begin_read ());
	return store.version_get (transaction);
}
std::unique_ptr<sgns::block_store> sgns::make_store ()
{
	return std::make_unique<sgns::ipfs_lite_store> ();
}