---
quick_id: 260603-dmz
slug: add-configurable-tokenid-fromuint256-end
status: complete
---

Added `TokenID::Endianness`, made `FromUint256` default to host byte order, and kept `BridgeRelayer` explicitly big-endian for ABI token IDs.
