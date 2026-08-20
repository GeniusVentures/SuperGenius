# Deferred Items

## 14-08 focused migration runtime verification

`migration_sync_test` builds successfully, but the focused migration run stops before its assertions because each node reports `GeniusNode initialization failed: Network initialization error`. This is outside the caller migration scope: it happens during node startup, before the migrated active-address/balance access. Reproduce with the Plan 14-08 focused filter after resolving the local node networking environment.

## 14-12 processing fixture runtime verification

With the standard local-listener permission, `processing_multi_test --gtest_filter='ProcessingMultiTest.ProcessOne'` constructs its nodes but reports `ACCOUNT_UNAVAILABLE` from the fixture's pre-readiness `RequireActiveGeneration(node_proc1)`, skips the test, and exits with status 139. `processing_nodes_test --gtest_filter='ProcessingNodesTest.PostProcessing'` and the bounded `node_example --terminal` smoke run similarly reached listener initialization but emitted no terminal result in the execution harness. All affected targets build successfully and Plan 14-12's deterministic ownership/migration audits pass. This is outside the caller-contract/CMake-wiring scope; do not weaken checked readiness assertions to mask it.
