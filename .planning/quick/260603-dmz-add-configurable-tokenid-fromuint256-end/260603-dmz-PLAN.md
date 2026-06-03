---
quick_id: 260603-dmz
slug: add-configurable-tokenid-fromuint256-end
status: in-progress
---

Add an endianness parameter to `TokenID::FromUint256`, default it to host order, update bridge mint token conversion to pass big-endian explicitly, and cover big/little behavior in tests.
