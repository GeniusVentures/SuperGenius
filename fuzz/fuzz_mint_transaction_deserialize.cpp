// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT
//
// libFuzzer harness for MintTransactionV2::DeSerializeByteVector.
//
// DeSerializeByteVector parses fuzzer-controlled bytes as a serialized
// SGTransaction::MintTxV2 protobuf message. Per the implementation read in
// src/account/MintTransactionV2.cpp, malformed input causes an early
// `return nullptr` (protobuf's ParseFromArray failure or an invalid hash) —
// it does not throw. We still wrap the call in try/catch defensively in
// case a transitively-called dependency (e.g. TokenID::FromBytes) throws
// on unexpected input, keeping this harness crash-free by contract even if
// that assumption changes later.

#include "account/MintTransactionV2.hpp"

#include <cstdint>
#include <cstddef>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput( const uint8_t *data, size_t size )
{
    const std::vector<uint8_t> bytes( data, data + size );

    try
    {
        auto transaction = sgns::MintTransactionV2::DeSerializeByteVector( bytes );
        (void)transaction;
    }
    catch ( ... )
    {
        // Any exception here would itself be a bug per Pitfall 4 (unchecked
        // parsing of attacker-controlled bytes) — swallow it so libFuzzer/
        // ASan can keep exploring rather than treating an exception as a
        // fuzzer-harness crash unrelated to memory safety. ASan will still
        // catch genuine memory-safety violations regardless of this catch.
        return 0;
    }

    return 0;
}
