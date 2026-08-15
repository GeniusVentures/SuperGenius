// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT
//
// libFuzzer harness for eth::abi::decode_log_data — decodes the non-indexed
// fields of a burn-event log's raw `data` bytes. Fuzzer bytes are used
// verbatim as the log data buffer against a fixed params vector matching the
// v2 bridgeOut event's non-indexed field layout: tokenId(uint), amount(uint),
// destChainID(uint), sgnsDestination(bytes32), destinationYOdd(bool).

#include "eth/abi_decoder.hpp"

#include <cstdint>
#include <cstddef>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput( const uint8_t *data, size_t size )
{
    const eth::codec::ByteBuffer buffer( data, data + size );

    static const std::vector<eth::abi::AbiParam> params = {
        { eth::abi::AbiParamKind::kUint, false, "tokenId" },
        { eth::abi::AbiParamKind::kUint, false, "amount" },
        { eth::abi::AbiParamKind::kUint, false, "destChainID" },
        { eth::abi::AbiParamKind::kBytes32, false, "sgnsDestination" },
        { eth::abi::AbiParamKind::kBool, false, "destinationYOdd" },
    };

    // Discard the result — we only care that decode_log_data never crashes,
    // hangs, or triggers an ASan finding on attacker-controlled bytes.
    auto result = eth::abi::decode_log_data( buffer, params );
    (void)result;

    return 0;
}
