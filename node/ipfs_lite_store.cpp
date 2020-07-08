/// \\file for implementing ipfs_lite_store class
/// \\author ruymaster

#include <secure/buffer.hpp>
#include <secure/versioning.hpp>
#include <crypto_lib/random_pool.hpp>
#include <lib/utility.hpp>

#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/polymorphic_cast.hpp>

#include "ipfs_lite_store.hpp"

#include <queue>

namespace sgns
{
    template <>
    void * ipfs_val::data () const
    {
        return value.mv_data;
    }

    template <>
    size_t ipfs_val::size () const
    {
        return value.mv_size;
    }

    template <>
    ipfs_val::db_val (size_t size_a, void * data_a) :
    value ({ size_a, data_a })
    {
    }

    template <>
    void ipfs_val::convert_buffer_to_value ()
    {
        value = { buffer->size (), const_cast<uint8_t *> (buffer->data ()) };
    }
}

sgns::ipfs_lite_store::ipfs_lite_store(){    

}
bool sgns::ipfs_lite_store::exists (sgns::transaction const & transaction_a, tables table_a, ipfs_val const & key_a) const
 {
    return false;
 }
int sgns::ipfs_lite_store::get (sgns::transaction const& transaction_a, tables table_a, ipfs_val const &key, ipfs_val const & value_a) const
{
    return 0;
}

int sgns::ipfs_lite_store::put (sgns::write_transaction const & transaction_a, tables table_a, ipfs_val const & key_a, const ipfs_val & value_a) const
{
    return 0;
}

int sgns::ipfs_lite_store::del (sgns::write_transaction const & transaction_a, tables table_a, ipfs_val const & key_a) const
 {
    return 0;
}

int sgns::ipfs_lite_store::drop (sgns::write_transaction const & transaction_a, tables table_a)
{
	return 0;
}

bool sgns::ipfs_lite_store::not_found (int status) const
{
	return false;
}

bool sgns::ipfs_lite_store::success (int status) const
{
	return false;
}

int sgns::ipfs_lite_store::status_code_not_found () const
{
	return 0;
}

bool sgns::ipfs_lite_store::copy_db (boost::filesystem::path const & destination_file)
{
	return false;
}

void sgns::ipfs_lite_store::rebuild_db (sgns::write_transaction const & transaction_a)
{
	
}

bool sgns::ipfs_lite_store::init_error () const
{
	return false;
}

sgns::write_transaction sgns::ipfs_lite_store::tx_begin_write (std::vector<sgns::tables> const &, std::vector<sgns::tables> const &)
{
	return sgns::write_transaction(nullptr);
}

sgns::read_transaction sgns::ipfs_lite_store::tx_begin_read ()
{
	return sgns::read_transaction(nullptr);
}

std::string sgns::ipfs_lite_store::vendor_get () const
{
	return "";
}

bool sgns::ipfs_lite_store::block_info_get (sgns::transaction const & transaction_a, sgns::block_hash const & hash_a, sgns::block_info & block_info_a) const
{
    return false;
}
void sgns::ipfs_lite_store::version_put (sgns::write_transaction const & transaction_a, int version_a)
{
    
}

void sgns::ipfs_lite_store::serialize_mdb_tracker (boost::property_tree::ptree & json, std::chrono::milliseconds min_read_time, std::chrono::milliseconds min_write_time)
{
	
}
std::shared_ptr<sgns::block> sgns::ipfs_lite_store::block_get_v14 (sgns::transaction const & transaction_a, sgns::block_hash const & hash_a, sgns::block_sideband_v14 * sideband_a, bool * is_state_v1) const
{
    return nullptr;
}
size_t sgns::ipfs_lite_store::count (sgns::transaction const & transaction_a, tables table_a) const
{
	return 0;
}

//template class sgns::block_store_partial<IPFS_val, ifps_lite_store>;   