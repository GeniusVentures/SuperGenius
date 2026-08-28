#include "UTXOStructs.hpp"

#include "account/proto/SGTransaction.pb.h"

#include "base/endian.h"
#include "base/hexutil.hpp"
#include <algorithm>
#include <cctype>

std::vector<uint8_t> sgns::InputUTXOInfo::SerializeForSigning() const
{
    auto little_ended = htole32( output_idx_ );

    std::vector<uint8_t> vec( 36 );

    std::copy( txid_hash_.begin(), txid_hash_.end(), vec.begin() );
    memcpy( &little_ended, vec.data() + 32, sizeof( little_ended ) );

    return vec;
}

bool sgns::utxo_address::IsEscrowLockAddress( std::string_view address )
{
    if ( address.size() != 66 || address.substr( 0, 2 ) != "0x" )
    {
        return false;
    }

    return std::all_of( address.begin() + 2, address.end(), []( unsigned char c ) { return std::isxdigit( c ) != 0; } );
}

bool sgns::utxo_address::IsAccountPublicKeyAddress( std::string_view address )
{
    // Account addresses are produced by base::hex_lower, so the lowercase-only form is the
    // canonical one; see sgns::base::IsHexAddress.
    return sgns::base::IsHexAddress( address );
}
