---
quick_id: 260603-dcg
slug: add-tokenid-fromuint256-with-endian-dete
status: complete
---

Implemented `TokenID::FromUint256` with host byte-order detection for 64-bit word conversion, replaced `BridgeRelayer`'s manual uint256 loop with the new factory, and added `token_id_test` coverage for zero, small, and multi-word values.

Verification:
- `clang++ -std=c++17 -Isrc -Ievmrelay/include -x c++ -fsyntax-only -`
- `clang++ -std=c++17 -Isrc -Ievmrelay/include -I../thirdparty_35/build/OSX/Debug/GTest/include -fsyntax-only test/src/account/token_id_test.cpp`
