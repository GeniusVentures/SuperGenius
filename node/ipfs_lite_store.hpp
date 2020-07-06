#ifndef SUPERGENIUS_IPFS_LITE_STORE_HPP
#define SUPERGENIUS_IPFS_LITE_STORE_HPP
#include <secure/blockstore_partial.hpp>
namespace sgns
{

typedef struct IPFS_val {
size_t		 mv_size;	/**< size of the data item */
void		*mv_data;	/**< address of the data item */
} IPFS_val;

class ipfs_lite_store : public block_store_partial<IPFS_val, ipfs_lite_store>
{
public:
    ipfs_lite_store();
    ~ipfs_lite_store();
    
};
}
#endif