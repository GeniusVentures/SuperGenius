/// \\file for implementing ipfs_lite_store class
/// \\author ruymaster
#include "ipfs_lite_store.hpp"

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