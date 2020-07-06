///====================================================================================
/// \\file ipfs_lite_store.hpp
/// \\brief Header file for storing block into ipfs-lite
/// \\date 07/06/2020
/// \\author ruymaster
///====================================================================================

#ifndef SUPERGENIUS_IPFS_LITE_STORE_HPP
#define SUPERGENIUS_IPFS_LITE_STORE_HPP
#include <secure/blockstore_partial.hpp>

namespace sgns
{

typedef struct IPFS_val {
size_t		 mv_size;	/**< size of the data item */
void		*mv_data;	/**< address of the data item */
} ipfs_val;

class ipfs_lite_store : public block_store_partial<ipfs_val, ipfs_lite_store>
{
public:
    ipfs_lite_store();
    bool exists (sgns::transaction const & transaction_a, tables table_a, ipfs_val const & key_a) const;
    int get (sgns::transaction const& transaction_a, tables table_a, ipfs_val const &key, ipfs_val const & value_a);
    int put (sgns::write_transaction const & transaction_a, tables table_a, ipfs_val const & key_a, const ipfs_val & value_a) const;
    int del (sgns::write_transaction const & transaction_a, tables table_a, ipfs_val const & key_a) const;

};
}
#endif