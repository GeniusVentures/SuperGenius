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
// wallets_store_impl (std::make_unique<sgns::mdb_wallets_store> (application_path_a / "wallets.ldb", config_a.lmdb_config)),
// wallets_store (*wallets_store_impl),
// wallets (wallets_store.init_error (), *this),
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

std::pair<sgns::uint128_t, sgns::uint128_t> sgns::node::balance_pending (sgns::account const & account_a)
{
	std::pair<sgns::uint128_t, sgns::uint128_t> result;
	auto transaction (store.tx_begin_read ());
	result.first = ledger.account_balance (transaction, account_a);
	result.second = ledger.account_pending (transaction, account_a);
	return result;
}

sgns::process_return sgns::node::process (sgns::block & block_a)
{
	auto transaction (store.tx_begin_write ({ tables::accounts, tables::cached_counts, tables::change_blocks, tables::frontiers, tables::open_blocks, tables::pending, tables::receive_blocks, tables::representation, tables::send_blocks, tables::state_blocks }, { tables::confirmation_height }));
	auto result (ledger.process (transaction, block_a));
	return result;
}

// namespace
// {
// class confirmed_visitor : public sgns::block_visitor
// {
// public:
// 	confirmed_visitor (sgns::transaction const & transaction_a, sgns::node & node_a, std::shared_ptr<sgns::block> const & block_a, sgns::block_hash const & hash_a) :
// 	transaction (transaction_a),
// 	node (node_a),
// 	block (block_a),
// 	hash (hash_a)
// 	{
// 	}
// 	virtual ~confirmed_visitor () = default;
// 	void scan_receivable (sgns::account const & account_a)
// 	{
// 		for (auto i (node.wallets.items.begin ()), n (node.wallets.items.end ()); i != n; ++i)
// 		{
// 			auto const & wallet (i->second);
// 			auto transaction_l (node.wallets.tx_begin_read ());
// 			if (wallet->store.exists (transaction_l, account_a))
// 			{
// 				sgns::account representative;
// 				sgns::pending_info pending;
// 				representative = wallet->store.representative (transaction_l);
// 				auto error (node.store.pending_get (transaction, sgns::pending_key (account_a, hash), pending));
// 				if (!error)
// 				{
// 					auto node_l (node.shared ());
// 					auto amount (pending.amount.number ());
// 					wallet->receive_async (block, representative, amount, [](std::shared_ptr<sgns::block>) {});
// 				}
// 				else
// 				{
// 					if (!node.store.block_exists (transaction, hash))
// 					{
// 						node.logger.try_log (boost::str (boost::format ("Confirmed block is missing:  %1%") % hash.to_string ()));
// 						debug_assert (false && "Confirmed block is missing");
// 					}
// 					else
// 					{
// 						node.logger.try_log (boost::str (boost::format ("Block %1% has already been received") % hash.to_string ()));
// 					}
// 				}
// 			}
// 		}
// 	}
// 	void state_block (sgns::state_block const & block_a) override
// 	{
// 		scan_receivable (block_a.hashables.link);
// 	}
// 	void send_block (sgns::send_block const & block_a) override
// 	{
// 		scan_receivable (block_a.hashables.destination);
// 	}
// 	void receive_block (sgns::receive_block const &) override
// 	{
// 	}
// 	void open_block (sgns::open_block const &) override
// 	{
// 	}
// 	void change_block (sgns::change_block const &) override
// 	{
// 	}
// 	sgns::transaction const & transaction;
// 	sgns::node & node;
// 	std::shared_ptr<sgns::block> block;
// 	sgns::block_hash const & hash;
// };
// }

void sgns::node::receive_confirmed (sgns::transaction const & transaction_a, std::shared_ptr<sgns::block> block_a, sgns::block_hash const & hash_a)
{
	// confirmed_visitor visitor (transaction_a, *this, block_a, hash_a);
	// block_a->visit (visitor);
}

void sgns::node::process_confirmed_data (sgns::transaction const & transaction_a, std::shared_ptr<sgns::block> block_a, sgns::block_hash const & hash_a, sgns::account & account_a, sgns::uint128_t & amount_a, bool & is_state_send_a, sgns::account & pending_account_a)
{
	// Faster account calculation
	account_a = block_a->account ();
	if (account_a.is_zero ())
	{
		account_a = block_a->sideband ().account;
	}
	// Faster amount calculation
	auto previous (block_a->previous ());
	auto previous_balance (ledger.balance (transaction_a, previous));
	auto block_balance (store.block_balance_calculated (block_a));
	if (hash_a != ledger.network_params.ledger.genesis_account)
	{
		amount_a = block_balance > previous_balance ? block_balance - previous_balance : previous_balance - block_balance;
	}
	else
	{
		amount_a = ledger.network_params.ledger.genesis_amount;
	}
	if (auto state = dynamic_cast<sgns::state_block *> (block_a.get ()))
	{
		if (state->hashables.balance < previous_balance)
		{
			is_state_send_a = true;
		}
		pending_account_a = state->hashables.link;
	}
	if (auto send = dynamic_cast<sgns::send_block *> (block_a.get ()))
	{
		pending_account_a = send->hashables.destination;
	}
}

// void sgns::node::process_confirmed (sgns::election_status const & status_a, std::shared_ptr<sgns::election> const & election_a, uint8_t iteration_a)
// {
// 	if (status_a.type == sgns::election_status_type::active_confirmed_quorum)
// 	{
// 		auto block_a (status_a.winner);
// 		auto hash (block_a->hash ());
// 		if (ledger.block_exists (block_a->type (), hash))
// 		{
// 			// Pausing to prevent this block being processed before adding to election winner details.
// 			confirmation_height_processor.pause ();
// 			confirmation_height_processor.add (hash);
// 			{
// 				active.add_election_winner_details (hash, election_a);
// 			}
// 			confirmation_height_processor.unpause ();
// 		}
// 		// Limit to 0.5 * 20 = 10 seconds (more than max block_processor::process_batch finish time)
// 		else if (iteration_a < 20)
// 		{
// 			iteration_a++;
// 			std::weak_ptr<sgns::node> node_w (shared ());
// 			alarm.add (std::chrono::steady_clock::now () + network_params.node.process_confirmed_interval, [node_w, status_a, iteration_a, election_a]() {
// 				if (auto node_l = node_w.lock ())
// 				{
// 					node_l->process_confirmed (status_a, election_a, iteration_a);
// 				}
// 			});
// 		}
// 	}
// }
void sgns::node::stop ()
{
	if (!stopped.exchange (true))
	{
		// logger.always_log ("Node stopping");
		// write_database_queue.stop ();
		// Cancels ongoing work generation tasks, which may be blocking other threads
		// No tasks may wait for work generation in I/O threads, or termination signal capturing will be unable to call node::stop()
		// distributed_work.stop ();
		// block_processor.stop ();
		// if (block_processor_thread.joinable ())
		// {
		// 	block_processor_thread.join ();
		// }
		// aggregator.stop ();
		// vote_processor.stop ();
		// active.stop ();
		// confirmation_height_processor.stop ();
		// network.stop ();
		// if (telemetry)
		// {
		// 	telemetry->stop ();
		// 	telemetry = nullptr;
		// }
		// if (websocket_server)
		// {
		// 	websocket_server->stop ();
		// }
		// bootstrap_initiator.stop ();
		// bootstrap.stop ();
		// port_mapping.stop ();
		// checker.stop ();
		// wallets.stop ();
		// stats.stop ();
		// worker.stop ();
		// work pool is not stopped on purpose due to testing setup
	}
}
sgns::node_flags const & sgns::inactive_node_flag_defaults ()
{
	static sgns::node_flags node_flags;
	node_flags.inactive_node = true;
	node_flags.read_only = true;
	node_flags.generate_cache.reps = false;
	node_flags.generate_cache.cemented_count = false;
	node_flags.generate_cache.unchecked_count = false;
	node_flags.disable_bootstrap_listener = true;
	node_flags.disable_tcp_realtime = true;
	return node_flags;
}

sgns::inactive_node::inactive_node (boost::filesystem::path const & path_a, sgns::node_flags const & node_flags_a) /*:*/
// io_context (std::make_shared<boost::asio::io_context> ()),
// alarm (*io_context),
// work (1)
{
// 	boost::system::error_code error_chmod;

// 	/*
// 	 * @warning May throw a filesystem exception
// 	 */
// 	boost::filesystem::create_directories (path_a);
// 	sgns::set_secure_perm_directory (path_a, error_chmod);
// 	sgns::daemon_config daemon_config (path_a);
// 	auto error = sgns::read_node_config_toml (path_a, daemon_config, node_flags_a.config_overrides);
// 	if (error)
// 	{
// 		std::cerr << "Error deserializing config file";
// 		if (!node_flags_a.config_overrides.empty ())
// 		{
// 			std::cerr << " or --config option";
// 		}
// 		std::cerr << "\n"
// 		          << error.get_message () << std::endl;
// 		std::exit (1);
// 	}

// 	auto & node_config = daemon_config.node;
// 	node_config.peering_port = sgns::get_available_port ();
// 	node_config.logging.max_size = std::numeric_limits<std::uintmax_t>::max ();
// 	node_config.logging.init (path_a);

// 	node = std::make_shared<sgns::node> (*io_context, path_a, alarm, node_config, work, node_flags_a);
// 	node->active.stop ();
}

sgns::inactive_node::~inactive_node ()
{
	node->stop ();
}

std::unique_ptr<sgns::block_store> sgns::make_store ()
{
	return std::make_unique<sgns::ipfs_lite_store> ();
}