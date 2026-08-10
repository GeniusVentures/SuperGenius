---
quick_id: 260603-dla
slug: fix-tokenid-fromuint256-to-use-host-inde
status: in-progress
---

Correct `TokenID::FromUint256` to serialize the numeric uint256 value to canonical big-endian bytes with shifts, matching the original `BridgeRelayer` behavior and avoiding host-endian memory interpretation.
