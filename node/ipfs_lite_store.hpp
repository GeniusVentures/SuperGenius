///====================================================================================
/// \\file ipfs_lite_store.hpp
/// \\brief Header file for storing block into ipfs-lite
/// \\date 07/06/2020
/// \\author ruymaster
///====================================================================================

#ifndef SUPERGENIUS_IPFS_LITE_STORE_HPP
#define SUPERGENIUS_IPFS_LITE_STORE_HPP
#include <secure/blockstore_partial.hpp>

#include <boost/filesystem.hpp>
#include <ipfs_lite/ipfs/impl/datastore_leveldb.hpp>



typedef struct IPFS_val {
size_t		 mv_size;	/**< size of the data item */
void		*mv_data;	/**< address of the data item */
} IPFS_val;

namespace sgns
{
    namespace filesystem
    {
        class path;
    }
}


namespace sgns
{
    namespace filesystem
    {
        class path;
    }
}


namespace sgns
{

    /** @brief A handle for an individual database in the DB environment. */
    typedef unsigned int	ipfs_dbi;
    
    using ipfs_val = db_val<IPFS_val>;
    using sgns::ipfs_lite::ipfs::IpfsDatastore;
    using sgns::ipfs_lite::ipfs::LeveldbDatastore;
    class ipfs_lite_store : public block_store_partial<IPFS_val, ipfs_lite_store>
    {
    private:
         std::shared_ptr<IpfsDatastore> datastore;
    public:
        ipfs_lite_store();
//        ipfs_lite_store (sgns::logger_mt &, boost::filesystem::path const &, sgns::txn_tracking_config const & txn_tracking_config_a = sgns::txn_tracking_config{}, std::chrono::milliseconds block_processor_batch_max_time_a = std::chrono::milliseconds (5000), sgns::lmdb_config const & lmdb_config_a = sgns::lmdb_config{}, size_t batch_size = 512, bool backup_before_upgrade = false);
        bool exists (sgns::transaction const & transaction_a, tables table_a, ipfs_val const & key_a) const;
        int get (sgns::transaction const& transaction_a, tables table_a, ipfs_val const &key, ipfs_val const & value_a) const;
        int put (sgns::write_transaction const & transaction_a, tables table_a, ipfs_val const & key_a, const ipfs_val & value_a) const;
        int del (sgns::write_transaction const & transaction_a, tables table_a, ipfs_val const & key_a) const;
        int drop (sgns::write_transaction const & transaction_a, tables table_a) override;
        
        sgns::write_transaction tx_begin_write (std::vector<sgns::tables> const & tables_requiring_lock = {}, std::vector<sgns::tables> const & tables_no_lock = {}) override;
	    sgns::read_transaction  tx_begin_read () override;

	    std::string vendor_get () const override;

	    bool block_info_get (sgns::transaction const &, sgns::block_hash const &, sgns::block_info &) const override;
	    void version_put (sgns::write_transaction const &, int) override;
    	void serialize_mdb_tracker (boost::property_tree::ptree &, std::chrono::milliseconds, std::chrono::milliseconds) override;

        // These are only use in the upgrade process.
	    std::shared_ptr<sgns::block> block_get_v14 (sgns::transaction const & transaction_a, sgns::block_hash const & hash_a, sgns::block_sideband_v14 * sideband_a = nullptr, bool * is_state_v1 = nullptr) const override;
        bool copy_db (boost::filesystem::path const & destination_file) override;
	    void rebuild_db (sgns::write_transaction const & transaction_a) override;
        bool init_error () const override;
        size_t count (sgns::transaction const & transaction_a, tables table_a) const override;
        
        bool not_found (int status) const override;
	    bool success (int status) const override;
	    int status_code_not_found () const override;

        /**
        * Maps head block to owning account
        * sgns::block_hash -> sgns::account
        */
        ipfs_dbi frontiers{ 0 };

        /**
        * Maps account v1 to account information, head, rep, open, balance, timestamp and block count. (Removed)
        * sgns::account -> sgns::block_hash, sgns::block_hash, sgns::block_hash, sgns::amount, uint64_t, uint64_t
        */
        ipfs_dbi accounts_v0{ 0 };

        /**
        * Maps account v0 to account information, head, rep, open, balance, timestamp and block count. (Removed)
        * sgns::account -> sgns::block_hash, sgns::block_hash, sgns::block_hash, sgns::amount, uint64_t, uint64_t
        */
        ipfs_dbi accounts_v1{ 0 };

        /**
        * Maps account v0 to account information, head, rep, open, balance, timestamp, block count and epoch. (Removed)
        * sgns::account -> sgns::block_hash, sgns::block_hash, sgns::block_hash, sgns::amount, uint64_t, uint64_t, sgns::epoch
        */
        ipfs_dbi accounts{ 0 };

        /**
        * Maps block hash to send block.
        * sgns::block_hash -> sgns::send_block
        */
        ipfs_dbi send_blocks{ 0 };

        /**
        * Maps block hash to receive block.
        * sgns::block_hash -> sgns::receive_block
        */
        ipfs_dbi receive_blocks{ 0 };

        /**
        * Maps block hash to open block.
        * sgns::block_hash -> sgns::open_block
        */
        ipfs_dbi open_blocks{ 0 };

        /**
        * Maps block hash to change block.
        * sgns::block_hash -> sgns::change_block
        */
        ipfs_dbi change_blocks{ 0 };

        /**
        * Maps block hash to v0 state block. (Removed)
        * sgns::block_hash -> sgns::state_block
        */
        ipfs_dbi state_blocks_v0{ 0 };

        /**
        * Maps block hash to v1 state block. (Removed)
        * sgns::block_hash -> sgns::state_block
        */
        ipfs_dbi state_blocks_v1{ 0 };

        /**
        * Maps block hash to state block.
        * sgns::block_hash -> sgns::state_block
        */
        ipfs_dbi state_blocks{ 0 };

        /**
        * Maps min_version 0 (destination account, pending block) to (source account, amount). (Removed)
        * sgns::account, sgns::block_hash -> sgns::account, sgns::amount
        */
        ipfs_dbi pending_v0{ 0 };

        /**
        * Maps min_version 1 (destination account, pending block) to (source account, amount). (Removed)
        * sgns::account, sgns::block_hash -> sgns::account, sgns::amount
        */
        ipfs_dbi pending_v1{ 0 };

        /**
        * Maps (destination account, pending block) to (source account, amount, version). (Removed)
        * sgns::account, sgns::block_hash -> sgns::account, sgns::amount, sgns::epoch
        */
        ipfs_dbi pending{ 0 };

        /**
        * Maps block hash to account and balance. (Removed)
        * block_hash -> sgns::account, sgns::amount
        */
        ipfs_dbi blocks_info{ 0 };

        /**
        * Representative weights. (Removed)
        * sgns::account -> sgns::uint128_t
        */
        ipfs_dbi representation{ 0 };

        /**
        * Unchecked bootstrap blocks info.
        * sgns::block_hash -> sgns::unchecked_info
        */
        ipfs_dbi unchecked{ 0 };

        /**
        * Highest vote observed for account.
        * sgns::account -> uint64_t
        */
        ipfs_dbi vote{ 0 };

        /**
        * Samples of online vote weight
        * uint64_t -> sgns::amount
        */
        ipfs_dbi online_weight{ 0 };

        /**
        * Meta information about block store, such as versions.
        * sgns::uint256_union (arbitrary key) -> blob
        */
        ipfs_dbi meta{ 0 };

        /*
        * Endpoints for peers
        * sgns::endpoint_key -> no_value
        */
        ipfs_dbi peers{ 0 };

        /*
        * Confirmation height of an account, and the hash for the block at that height
        * sgns::account -> uint64_t, sgns::block_hash
        */
        ipfs_dbi confirmation_height{ 0 };

    };
    template <>
    void * ipfs_val::data () const;
    template <>
    size_t ipfs_val::size () const;
    template <>
    ipfs_val::db_val (size_t size_a, void * data_a);
    template <>
    void ipfs_val::convert_buffer_to_value ();
    extern template class block_store_partial<IPFS_val, ipfs_lite_store>;
}
#endif