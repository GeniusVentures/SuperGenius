# SuperGenius (GNUS.AI Super Genius blockchain)

C++ blockchain project: `src/`, `evmrelay`, `gRPCForSuperGenius`, `ProofSystem`, `GeniusKDF`, build logic under `cmake/`. Check `AgentDocs/` first (AGENT_MISTAKES.md, Architecture.md, CHECKPOINT.md) — per the global rules — and see the global CLAUDE.md for coding standards.

## Build-System Pre-PR Checklist

Run `/gsd-code-review` with this checklist before any PR that touches builds (CMakeLists, *.cmake, build scripts, codegen wiring). These are the cross-platform / cross-tree failure modes that pass on the dev machine and only surface in external review:

- **Artifact routing:** a SHARED-library target needs BOTH `RUNTIME_OUTPUT_DIRECTORY` (Windows DLL) and `LIBRARY_OUTPUT_DIRECTORY` (macOS dylib / Linux .so), PLUS per-config `<PROP>_<CONFIG>` entries — multi-config generators (Xcode, Ninja Multi-Config, MSVC) append `$<CONFIG>` to the plain directories, landing artifacts in `plugins/Debug/`-style subdirs that loaders never scan.
- **Interpreter discovery:** NEVER `find_program(<var> python3)` — native Windows exposes `python.exe` / `py.exe`, and the lookup fails there even when Python is installed. Use `find_package(Python3 COMPONENTS Interpreter REQUIRED)` and alias to `Python3_EXECUTABLE`.
- **No POSIX shell in build commands:** `sh -c` breaks native Windows. Drive multi-command logic with `${CMAKE_COMMAND} -DPYTHON=... -P driver.cmake` script mode.
- **Absent submodules:** a non-recursive clone must still configure. Gate every `REQUIRED` lookup and `add_subdirectory` on an existence check — nothing tool-`REQUIRED` may run before the submodule guard.
- **Default-off opt-ins:** wiring behind an `option(... OFF)` must execute nothing in a default build — audit `ALL` custom targets chained on generation / dependency resolution.
- **Standalone subdir configures:** any `add_subdirectory`-able directory may be configured alone. `if(TARGET ...)`-guard every cross-directory link dependency; unguarded bare names degrade to `-l` linker flags.
- **Generated sources:** a target compiling generated files registers only when its producer wiring exists (flag or `TARGET` check); otherwise skip with a `message(STATUS ...)` — never fail configure on a file only another wiring produces.
- **Spec-driven regeneration:** generated code must regenerate when its spec changes (`CMAKE_CONFIGURE_DEPENDS` on the spec + newer-than stamp), including fresh trees (configure-time pre-generation before the globs).

## Architecture Constraints (always follow)

Adapted from the standing global constraints, mapped to this codebase's structure.

- **Separation of concerns:** domain logic (`src/` services — transactions, consensus, bridge, registries) · persistence (CRDT datastore / globaldb layers) · transport surface (gRPC services in `gRPCForSuperGenius/`, `evmrelay/`) · wiring (CMake, `cmake/`). Name the one concern of the file you edit.
- **Encapsulation:** public contracts only; read another module's CRDT entries, caches, and registries through its functions.
- **Cohesion / coupling:** one rule change touches one module.
- **DRY:** grep before writing logic; one home per rule, threshold, format, or wire-schema fact. Do not abstract coincidental similarity.
- **KISS / YAGNI:** simplest working shape; free function over class; no speculative hooks, flags, or frameworks.
- **Single responsibility:** if you describe it with "and", split it. Names say intent; comments say why.
- **Depend on contracts:** domain code takes and returns plain values; it never includes RPC/transport service headers — wire types stay at the boundary.
- **Composition over inheritance** (matches the GoF rules in the global CLAUDE.md) · open/closed only where change has happened twice · Demeter (no `a.b.c.d` chains) · fail fast (validate at edges, never swallow errors) · optimize for deletion · boring tech.

The hard invariants (never violate):

1. **One owning module per domain** — each business domain's logic (transactions, consensus/finality, bridge relaying, validator registry, minting…) lives in one module; everyone else calls it. Consolidate a scattered domain before adding to it. Extend the domain's existing module; create a new one only when you can say why the old one cannot own it. A new rule goes in its domain's owner, not in the first feature that needs it; a function-level include to dodge a cycle means the logic is in the wrong module.
2. **Never duplicate logic** — second use of existing logic: (1) move it to a shared module, (2) switch the original caller, tests green, no behaviour change, (3) then build the new use. First grep for the expression (the arithmetic, the format string, the threshold) and list every copy; the move switches them all or the commit names each one left and why. A new helper beside old copies is one more duplicate. The move keeps each caller's exact results (guards, rounding, clamps); any behaviour change is its own commit. Same for wire schemas (one fact, one field) and repeated test scaffolding (one helper per repeated piece).
3. **No business logic in the transport layer** — gRPC handlers, serializers, and RPC view builders only marshal and report values; they never compute fees, rules, or classifications. Arithmetic or rules on business data there is a bug; move it to the owning module. Who sees a value is decided in the domain module, not by a handler branch.

How to work:

- **Refactor first, then change:** a behaviour-preserving refactor with tests green, then the change. Never both in one unverifiable diff.
- **Gates, not promises.** A prompted "never" alone does not protect the source of truth; a rule that matters is enforced by ctest, CI, or the `/gsd-code-review` gate.

## C++ Engineering Constraints

These are design constraints, not a checklist.

The **GNUS C++ Coding Standards are authoritative** for C++ syntax, naming, layout, language use, class design, error handling, file layout, includes, platform abstraction, and tooling. Do not override them with a general design principle or a local preference.

When two design principles conflict, choose the option that creates the lowest future cost **in this repository** while preserving correctness, clarity, and the existing architecture.

### Core rule: refactor first, then change behavior

When existing structure prevents a clean change:

1. Make the smallest behavior-preserving refactor needed.
2. Run the relevant tests and verification.
3. Only then change behavior.

Do not mix a structural rewrite and a behavioral change into one diff when they can be separated.

A refactor must preserve observable behavior, including error results, ordering, rounding, limits, ownership, lifetime, serialization, protocol behavior, and thread-safety.

---

### Design principles

1. **Separation of concerns**  
   Give each module one kind of work. Keep domain rules, persistence, networking, protocol handling, platform integration, serialization, UI/API adaptation, and infrastructure separate where practical.

2. **Encapsulation and information hiding**  
   Expose the smallest complete public interface. Hide storage, caches, internal containers, implementation types, synchronization, and other details behind that interface.

3. **High cohesion, loose coupling**  
   Code that changes for the same reason belongs together. Independent parts communicate through narrow, explicit contracts.

4. **DRY means one source of truth**  
   Keep one authoritative representation of each rule, formula, threshold, protocol constant, schema fact, encoding rule, or other piece of knowledge.  
   Do not abstract code merely because two blocks look similar.

5. **KISS**  
   Prefer the simplest design that fully solves the current problem. Avoid extra layers, indirection, factories, wrappers, templates, or abstractions unless they remove real duplication or isolate a real dependency.

6. **Single responsibility**  
   A class, module, or function should have one clear reason to change. If its purpose requires unrelated responsibilities joined by "and", split them.

7. **Depend on contracts, not implementation details**  
   Higher-level policy must not reach through another component's internals. Use the existing public interface or introduce the smallest suitable interface when a real boundary exists.

8. **YAGNI**  
   Do not add speculative features, extension points, configuration flags, abstractions, or "future use" APIs without a current requirement.

9. **Composition over implementation inheritance**  
   Prefer composition when combining behavior. Use inheritance where the relationship is genuinely "is-a" or where an abstract interface defines the required contract.

10. **Open/Closed, with evidence**  
    Do not create extension frameworks in anticipation of change. Introduce an extension boundary when repeated real changes show that a stable boundary exists.

11. **Law of Demeter**  
    Do not reach through chains of objects to manipulate distant internals. Use named intermediate values and the owning object's public contract.

12. **Fail early and explicitly**  
    Validate input and invariants at boundaries. Return errors through the project's established error mechanism. Never silently swallow failures.

13. **Make invalid states hard to represent**  
    Use types, scoped enums, constructors/factories, ownership types, and validation to prevent invalid combinations where doing so keeps the code simpler and clearer.

14. **Optimize for deletion**  
    Prefer code that can later be removed without affecting unrelated components. Avoid hidden dependencies and unnecessary framework code.

---

## Hard invariants

### 1. One owning module per domain rule

Every domain rule must have one clear owner.

Examples include:

- pricing and valuation
- consensus and quorum rules
- peer selection
- transaction validation
- serialization and wire formats
- trust and reputation
- token/accounting rules
- scheduling and assignment
- protocol limits and thresholds

Before adding a rule, find its existing owner.

If the rule already exists elsewhere, extend that owner rather than adding another implementation beside it.

If related logic is scattered, consolidate it before extending it where doing so can be done safely and independently.

Creating a new module requires a clear reason why the existing owner cannot own the rule.

A local include, helper, callback, global, or dependency inversion used only to avoid an architectural cycle usually means the responsibility is in the wrong place. Fix the ownership rather than hiding the cycle.

---

### 2. Never duplicate domain knowledge

Before writing new business or protocol logic, search the repository for:

- the formula
- constant or threshold
- enum or classification
- validation rule
- serialization rule
- format string
- state transition
- retry/backoff rule
- error mapping
- protocol field
- equivalent helper

On the second real use of the same knowledge:

1. move the existing implementation to its proper owner;
2. switch existing callers to it;
3. verify behavior is unchanged;
4. then add the new caller.

Do not create a shared helper while leaving old copies behind.

When consolidating logic, preserve all existing behavior: guards, integer semantics, precision, rounding, overflow handling, ordering, limits, error values, and side effects.

A behavior change belongs in a separate change.

---

### 3. No domain logic in adapters or presentation layers

API handlers, RPC adapters, CLI code, UI/view code, serializers, transport handlers, and other boundary code should translate data and delegate work.

They must not independently implement domain rules such as pricing, validation, classification, consensus decisions, ownership rules, or protocol policy.

Compute domain results in the owning C++ module and pass the result outward.

---

### 4. Ownership and lifetime must be explicit

Use RAII.

Prefer stack allocation.

Use `std::unique_ptr` for exclusive heap ownership and `std::shared_ptr` only when ownership is genuinely shared.

A raw pointer or reference should normally express non-owning access, not ownership.

Do not introduce raw `new` or `delete` in application code.

Do not expose handles to private mutable internals.

Make object lifetime and ownership clear from the interface.

---

### 5. Prefer the smallest C++ abstraction that fits

For stateless operations that do not require private class data, prefer non-member, non-friend functions as required by the GNUS C++ standards.

Use a class when state, invariants, ownership, or encapsulation require one.

Use an abstract interface when callers need to depend on a contract rather than an implementation.

Do not create a class merely to hold unrelated helper functions.

Do not create an interface for a single implementation unless it represents a real architectural boundary, test seam, platform boundary, or dependency that callers must not own directly.

---

### 6. Keep public contracts small and stable

Public headers are contracts.

Do not expose internal containers, synchronization primitives, storage layouts, implementation-only types, or third-party details without need.

Minimize header dependencies and forward-declare types where the GNUS standards allow it.

A caller should not need to understand implementation details to use a component correctly.

Changes to public headers deserve more scrutiny than equivalent `.cpp` changes because they increase coupling and build impact.

---

### 7. Errors are part of the contract

Use the project's established `outcome::result<T>` pattern for fallible operations, especially hot paths.

Do not use exceptions as an informal alternate error channel where the surrounding code uses `outcome::result`.

Do not convert errors into success, empty values, logs, or ignored return values unless the contract explicitly requires that behavior.

Use assertions for programmer errors and invariants, not for expected runtime failures.

Destructors must never throw.

---

### 8. Preserve portability

Keep platform-specific behavior behind the project's platform abstraction.

Do not add OS-specific `#ifdef` branches to normal source files.

Use `Platform.hpp`, platform-specific implementations, and CMake source/include selection as required by the GNUS standards.

Do not add compiler-specific behavior unless it is isolated behind the same kind of boundary.

---

## C++ implementation rules

For every new or modified C++ file:

- Target **C++17 only**. Do not introduce C++20 or later features.
- Follow the repository `.clang-format`; do not hand-format against it.
- Follow Required `.clang-tidy` checks.
- Use the GNUS naming conventions for new code.
- Use Allman braces and always brace control statements.
- Initialize variables at declaration.
- Use `nullptr` for null pointers.
- Use explicit C++ casts where conversion is required.
- Preserve `const` correctness.
- Use `enum class` instead of unscoped enums.
- Do not use `goto`.
- Do not compare floating-point values directly for equality.
- Prefer `constexpr` or `inline constexpr` to value macros.
- Use `.hpp` for C++ headers and `.cpp` for C++ sources.
- Keep public functions and interfaces documented in the header.
- Give every source/header the required Doxygen-compatible file header.
- Prefer standard algorithms and range-based loops when they make the code clearer.
- Keep functions focused; roughly 100 lines is the upper guideline, not a target.
- Include only what a header directly needs.
- Use project-root-relative paths for internal includes.
- Use angle brackets for standard and third-party headers.
- Keep class member ordering consistent with the GNUS standard.

Do not invent a competing local C++ style.

---

## Change discipline

Before implementing a non-trivial change:

1. Find the domain owner.
2. Search for existing implementations of the same rule or knowledge.
3. Identify the public contract that should own the behavior.
4. Refactor existing code first if the new behavior would otherwise create duplication or cross-domain coupling.
5. Keep that refactor behavior-preserving.
6. Implement the behavioral change.
7. Add or update tests at the owning module's boundary.

Do not use function-level includes, globals, singletons, callbacks, friend access, inheritance, templates, macros, or new abstraction layers merely to route around a bad dependency. Fix the dependency when practical.

---

## Verification is a gate, not a promise

A rule that matters should be enforced by tests, the compiler, `clang-tidy`, `clang-format`, CMake, CI, or another automated check whenever practical.

Before declaring C++ work complete:

1. format changed C++ files with the repository `clang-format`;
2. run `clang-tidy` on changed files and clear Required findings;
3. build the affected targets without errors or warnings;
4. run the relevant tests;
5. confirm that a refactor-only change did not alter behavior.

Never claim a check passed unless it was actually run.

If a required check cannot be run, state exactly which check was not run and why.
