---
quick_id: 260603-dor
slug: implement-full-tokenid-fromuint256-tests
status: in-progress
---

Strengthen `TokenID::FromUint256` tests so explicit big-endian and little-endian modes are both checked against full 32-byte expected arrays for the same multi-word uint256 value.
