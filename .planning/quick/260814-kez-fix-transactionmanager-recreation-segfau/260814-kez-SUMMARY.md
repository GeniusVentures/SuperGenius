---
status: complete
commit: 6bb99959
---

# TransactionManager recreation segfault fix

- Added ownership-aware cleanup for certificate, subject, resource-admission, and proposal-cleanup consensus handlers.
- Added the missing `Blockchain` unregister forwarding methods.
- Hardened previous-hash restart calls with `ASSERT_NO_FATAL_FAILURE`.
- Added `TransactionManagerRecoveryTest.RecreatesManagerUsingSameBlockchain` as an isolated regression test.

## Verification

- `cmake --build build/OSX/Release --target transaction_manager_pending_lifecycle_test -j 4` — passed.
- `TransactionManagerRecoveryTest.RecreatesManagerUsingSameBlockchain` — passed.
- The two original previous-hash tests no longer segfault. They currently fail separate pre-existing certificate/history assertions on this branch, before or after recreation.
