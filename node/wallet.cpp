#include "wallet.hpp"

sgns::wallets::wallets (bool error_a, sgns::node & node_a) :
observer ([](bool) {}),
node (node_a),
env (boost::polymorphic_downcast<sgns::mdb_wallets_store *> (node_a.wallets_store_impl.get ())->environment),
stopped (false),
watcher (std::make_shared<sgns::work_watcher> (node_a)),
thread ([this]() {
	sgns::thread_role::set (sgns::thread_role::name::wallet_actions);
	do_wallet_actions ();
})
{
	sgns::unique_lock<std::mutex> lock (mutex);
	if (!error_a)
	{
		auto transaction (tx_begin_write ());
		auto status (mdb_dbi_open (env.tx (transaction), nullptr, MDB_CREATE, &handle));
		split_if_needed (transaction, node.store);
		status |= mdb_dbi_open (env.tx (transaction), "send_action_ids", MDB_CREATE, &send_action_ids);
		debug_assert (status == 0);
		std::string beginning (sgns::uint256_union (0).to_string ());
		std::string end ((sgns::uint256_union (sgns::uint256_t (0) - sgns::uint256_t (1))).to_string ());
		sgns::store_iterator<std::array<char, 64>, sgns::no_value> i (std::make_unique<sgns::mdb_iterator<std::array<char, 64>, sgns::no_value>> (transaction, handle, sgns::mdb_val (beginning.size (), const_cast<char *> (beginning.c_str ()))));
		sgns::store_iterator<std::array<char, 64>, sgns::no_value> n (std::make_unique<sgns::mdb_iterator<std::array<char, 64>, sgns::no_value>> (transaction, handle, sgns::mdb_val (end.size (), const_cast<char *> (end.c_str ()))));
		for (; i != n; ++i)
		{
			sgns::wallet_id id;
			std::string text (i->first.data (), i->first.size ());
			auto error (id.decode_hex (text));
			debug_assert (!error);
			debug_assert (items.find (id) == items.end ());
			auto wallet (std::make_shared<sgns::wallet> (error, transaction, *this, text));
			if (!error)
			{
				items[id] = wallet;
			}
			else
			{
				// Couldn't open wallet
			}
		}
	}
	// Backup before upgrade wallets
	bool backup_required (false);
	if (node.config.backup_before_upgrade)
	{
		auto transaction (tx_begin_read ());
		for (auto & item : items)
		{
			if (item.second->store.version (transaction) != sgns::wallet_store::version_current)
			{
				backup_required = true;
				break;
			}
		}
	}
	if (backup_required)
	{
		const char * store_path;
		mdb_env_get_path (env, &store_path);
		const boost::filesystem::path path (store_path);
		sgns::mdb_store::create_backup_file (env, path, node_a.logger);
	}
	for (auto & item : items)
	{
		item.second->enter_initial_password ();
	}
	if (node_a.config.enable_voting)
	{
		lock.unlock ();
		ongoing_compute_reps ();
	}
}