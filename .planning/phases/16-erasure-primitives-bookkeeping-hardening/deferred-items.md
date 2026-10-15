# Deferred Items — Phase 16

Out-of-scope discoveries logged during execution (not fixed, per executor scope boundary).

| Date | Found in | Item | Why deferred |
|------|----------|------|--------------|
| 2026-10-15 | 16-01 (Task 3 verification — full `crdt_test` run) | Repo-wide: fresh-DB test suites leave their RocksDB directories untracked in the repo root (e.g. `supergenius_crdt_set_test_set_value/`, `supergenius_crdt_datastore_testaux*/`). `.gitignore` covers `*.db` but not these directories. 16-01 added an entry only for its own new suite's dir; the ~15 pre-existing suites' dirs remain unignored. | Pre-existing hygiene issue affecting many suites outside this plan's files; a repo-wide ignore pattern (`supergenius_*_test*/` or similar) is a shared-config decision. |
| 2026-10-15 | 16-01 (Task 1 verification) | `clang-tidy` cannot consume `build/OSX/Release/compile_commands.json` in this environment: each TU has two compiler jobs (x86_64 + arm64 universal entries) and clang-tidy aborts with "expected exactly one compiler job". Affects any future executor relying on compile-DB-driven tidy runs on this machine. | Toolchain/environment issue (CMake universal-binary compile DB), not a source issue; build system clears `CXX_CLANG_TIDY` on targets anyway. Compensated with strict `-Wall -Wextra` clean rebuilds. |
