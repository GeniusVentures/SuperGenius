# Deferred Items

## 14-08 focused migration runtime verification

`migration_sync_test` builds successfully, but the focused migration run stops before its assertions because each node reports `GeniusNode initialization failed: Network initialization error`. This is outside the caller migration scope: it happens during node startup, before the migrated active-address/balance access. Reproduce with the Plan 14-08 focused filter after resolving the local node networking environment.
