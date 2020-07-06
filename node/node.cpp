#include "node.hpp"
#include "node.hpp"
#include "ipfs_lite_store.hpp"
sgns::node::node (sgns::alarm & alarm_a, sgns::node_config const & config_a, sgns::node_flags flags_a) :
config (config_a),
flags (flags_a),
alarm (alarm_a),
store_impl (sgns::make_store ()),
store (*store_impl),
ledger (store, stats, flags_a.generate_cache)
{
    check_genesis();
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

void sgns::node::check_genesis() {
        // First do a pass with a read to see if any writing needs doing, this saves needing to open a write lock (and potentially blocking)
		auto is_initialized (false);
		{
			auto transaction (store.tx_begin_read ());
			is_initialized = (store.latest_begin (transaction) != store.latest_end ());
		}
        sgns::genesis genesis;
		if (!is_initialized)
		{
			release_assert (!flags.read_only);
			auto transaction (store.tx_begin_write ({ tables::accounts, tables::cached_counts, tables::confirmation_height, tables::frontiers, tables::open_blocks }));
			// Store was empty meaning we just created it, add the genesis block
			store.initialize (transaction, genesis, ledger.cache);
		}

		if (!ledger.block_exists (genesis.hash ()))
		{
			std::stringstream ss;
			ss << "Genesis block not found. Make sure the node network ID is correct.";
			if (network_params.network.is_beta_network ())
			{
				ss << " Beta network may have reset, try clearing database files";
			}
			auto str = ss.str ();

			// logger.always_log (str);
			std::cerr << str << std::endl;
			std::exit (1);
		}
}

sgns::block_hash sgns::node::latest (sgns::account const & account_a)
{
	auto transaction (store.tx_begin_read ());
	return ledger.latest (transaction, account_a);
}

sgns::uint128_t sgns::node::balance (sgns::account const & account_a)
{
	auto transaction (store.tx_begin_read ());
	return ledger.account_balance (transaction, account_a);
}

std::shared_ptr<sgns::block> sgns::node::block (sgns::block_hash const & hash_a)
{
	auto transaction (store.tx_begin_read ());
	return store.block_get (transaction, hash_a);
}
sgns::process_return sgns::node::process (sgns::block & block_a)
{
	auto transaction (store.tx_begin_write ({ tables::accounts, tables::cached_counts, tables::change_blocks, tables::frontiers, tables::open_blocks, tables::pending, tables::receive_blocks, tables::representation, tables::send_blocks, tables::state_blocks }, { tables::confirmation_height }));
	auto result (ledger.process (transaction, block_a));
	return result;
}

std::unique_ptr<sgns::block_store> sgns::make_store ()
{
	return std::make_unique<sgns::ipfs_lite_store> ();
}