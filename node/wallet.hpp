#ifndef SUPERGENIUS_NODE_WALLET_HPP
#define SUPERGENIUS_NODE_WALLET_HPP
#include <secure/blockstore.hpp>
#include <secure/common.hpp>
namespace sgns
{
class node;
class wallet_store final
{

};
class wallet final : public std::enable_shared_from_this<sgns::wallet>
{
    public:

};


class wallets final
{
public:
	wallets (bool, sgns::node &);
	~wallets ();
	std::shared_ptr<sgns::wallet> open (sgns::wallet_id const &);
	std::shared_ptr<sgns::wallet> create (sgns::wallet_id const &);
	bool search_pending (sgns::wallet_id const &);
	void search_pending_all ();
	void destroy (sgns::wallet_id const &);
	void reload ();
	void do_wallet_actions ();
	void queue_wallet_action (sgns::uint128_t const &, std::shared_ptr<sgns::wallet>, std::function<void(sgns::wallet &)> const &);
	void foreach_representative (std::function<void(sgns::public_key const &, sgns::raw_key const &)> const &);
	bool exists (sgns::transaction const &, sgns::public_key const &);
	void stop ();
	void clear_send_ids (sgns::transaction const &);
	// sgns::wallet_representative_counts rep_counts ();
	bool check_rep (sgns::account const &, sgns::uint128_t const &, const bool = true);
	void compute_reps ();
	void ongoing_compute_reps ();
	void split_if_needed (sgns::transaction &, sgns::block_store &);
	// void move_table (std::string const &, MDB_txn *, MDB_txn *);
	sgns::network_params network_params;
	std::function<void(bool)> observer;
	std::unordered_map<sgns::wallet_id, std::shared_ptr<sgns::wallet>> items;
	std::multimap<sgns::uint128_t, std::pair<std::shared_ptr<sgns::wallet>, std::function<void(sgns::wallet &)>>, std::greater<sgns::uint128_t>> actions;
	sgns::locked<std::unordered_map<sgns::account, sgns::root>> delayed_work;
	std::mutex mutex;
	std::mutex action_mutex;
	sgns::condition_variable condition;
	// sgns::kdf kdf;
	// MDB_dbi handle;
	// MDB_dbi send_action_ids;
	// sgns::node & node;
	// sgns::mdb_env & env;
	std::atomic<bool> stopped;
	// std::shared_ptr<sgns::work_watcher> watcher;
	std::thread thread;
	static sgns::uint128_t const generate_priority;
	static sgns::uint128_t const high_priority;
	/** Start read-write transaction */
	sgns::write_transaction tx_begin_write ();

	/** Start read-only transaction */
	sgns::read_transaction tx_begin_read ();

private:
	std::mutex counts_mutex;
	// sgns::wallet_representative_counts counts;
};

std::unique_ptr<container_info_component> collect_container_info (wallets & wallets, const std::string & name);

}
#endif