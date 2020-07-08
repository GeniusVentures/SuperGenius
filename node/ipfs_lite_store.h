#ifndef IPFS_LITE_STORE_H
#define IPFS_LITE_STORE_H

#include <sys/types.h>

    typedef struct IPFS_val {
    size_t		 mv_size;	/**< size of the data item */
    void		*mv_data;	/**< address of the data item */
    } IPFS_val;

#endif