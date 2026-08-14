---
quick_id: 260814-kez
status: complete
---

# Fix TransactionManager recreation segfault

1. Track ownership of each consensus callback registered by `TransactionManager` and unregister only owned callbacks during `Stop()`.
2. Expose the missing proposal-cleanup and resource-admission unregister operations through `Blockchain`.
3. Make restart-test helper failures fatal to the calling test and run both previous-hash restart tests.
