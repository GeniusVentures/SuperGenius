---
quick_id: 260603-dla
slug: fix-tokenid-fromuint256-to-use-host-inde
status: complete
---

Replaced host-memory byte extraction in `TokenID::FromUint256` with value-level big-endian shifts, preserving the original `BridgeRelayer` semantics while centralizing the conversion.
