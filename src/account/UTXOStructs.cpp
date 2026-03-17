#include "UTXOStructs.hpp"

#include "account/proto/SGTransaction.pb.h"

#include "base/endian.h"

std::vector<uint8_t> sgns::InputUTXOInfo::SerializeForSigning() const
{
    auto little_ended = htole32( output_idx_ );

    std::vector<uint8_t> vec( 36 );

    std::copy( txid_hash_.begin(), txid_hash_.end(), vec.begin() );
    memcpy( &little_ended, vec.data() + 32, sizeof( little_ended ) );

    return vec;
}
