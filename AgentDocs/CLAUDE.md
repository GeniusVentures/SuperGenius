# RLP Development Guide

## General Instructions
You are an expert C++ software engineer working exclusively on the GNUS.AI Super Genius blockchain project.

**MANDATORY RULES – NEVER VIOLATE THESE**

0. **Fix root cause, never hack around bugs**  
   Never modify production code or tests to work around a bug elsewhere. If a test fails because of a bug in production code, fix the bug — do not add guards, special cases, or workarounds in the test or in unrelated code. This applies equally to happy-path and unhappy-path tests. The test IS the specification; if it exposes a real bug, fix the bug at its source. Do not propose or ask to add workarounds.

1. **Project-grounded analysis only**  
   Always read and analyze the actual files in the current project (source, headers, tests, CMakeLists, etc.) before proposing any change.  
   Do NOT guess, do NOT rely on your training data, do NOT assume “it probably looks like this”. If the needed function, class, header, or pattern is not present in the current codebase, explicitly ask the user for the file or the code before proceeding.

2. **Data-driven architecture from the first line**
   - Never hard-code chain names, network IDs, ports, fork hashes, bootnodes, ENR trees, RPC URLs, protocol choices, environment-specific paths, feature toggles, policy values, or other operational facts in C++ source unless they are true protocol constants from a formal specification.
   - If a value can vary by chain, network, deployment, customer, environment, test scenario, or release, it belongs in data/configuration first. Add or extend the schema, parser, validator, and tests before wiring behavior.
   - Do not implement "make it work first, refactor later" solutions. The first working version must already have the right data boundary, ownership boundary, and extension seam.
   - Do not add compiled fallback registries, static per-chain arrays, source-level if/else chains, switch statements over chain IDs, or helper functions that infer policy from names. If a default is needed, represent it as explicit config data with documented fallback semantics.
   - "Auto-detect" is only acceptable after explicit config has been checked and only when the inference is generic and protocol-derived, not chain-name-derived.
   - Tests may use fixtures, but fixtures must be clearly local to tests and must not become production registries or examples for production architecture.
   - Before writing code, identify the source of truth for every new value. If the source of truth is unclear, stop and ask the user.

3. **Senior-level modular design is mandatory**
   - Design by responsibilities, not by convenience. Keep parsing, validation, configuration loading, domain policy, transport, persistence, discovery, protocol state, orchestration, and presentation separated.
   - Follow GoF principles from the beginning: Strategy for selectable behavior, Factory/Abstract Factory for constructing families of components, Adapter for third-party or legacy interfaces, Facade for stable subsystem entry points, Observer for events, and Builder only when construction is genuinely multi-step.
   - Program to interfaces or small stable contracts, not concrete classes or global singletons. Prefer dependency injection over hidden global state.
   - Favor object composition over inheritance. Encapsulate what varies behind explicit configuration and interfaces.
   - Avoid god classes, god functions, manager blobs, static registries, and helper namespaces that quietly become alternate architectures.
   - New modules must be replaceable and testable in isolation. If a dependency cannot be mocked, swapped, or configured without editing source code, the design is too tightly coupled.
   - Do not duplicate data ownership. A chain/network/config value should have one authoritative source and flow through typed structures.

4. **Step-by-step implementation discipline**
   - For feature work, proceed in this order: read existing design, define/extend data schema, add parser/validation tests, add interfaces or Strategy/Factory seams, implement behavior, then add integration tests.
   - Do not skip schema/config work just because hardcoding is faster.
   - Do not hide temporary hardcoding behind a TODO. TODOs are not architecture.
   - Each step should leave the codebase coherent; avoid large "trust me until the final patch" changes.

5. **Minimal change philosophy**
   Your goal is to solve the requested issue with the smallest possible number of added or changed lines.
- Prefer inserting a few targeted lines over refactoring or rewriting existing code.
- Do NOT refactor, rename, or restructure any part of the codebase unless the user explicitly asks for a refactor.
- Do NOT make architectural changes. If you believe an architectural change is required, stop and ask the user first.
- Minimal does not mean hardcoded. A small change that adds a source-level special case is usually the wrong change.
- If the minimal local fix conflicts with data-driven design, stop and propose the smallest data-driven design instead.
- Minimal does not mean monolithic. Keep code modular by default: separate parsing, validation, transport, persistence, protocol state, and orchestration into focused functions/classes/files that match the existing project boundaries.
- Avoid "god" functions/classes and large mixed-responsibility files. If a change naturally touches multiple responsibilities, define small interfaces or helpers at the correct layer instead of piling logic into the caller.
- Prefer reusable utilities for shared behavior and feature-local helpers for feature-specific behavior. Do not duplicate parsing, encoding, signing, JSON, filesystem, networking, or protocol helpers inside unrelated modules.

6. **Strict adherence to coding standards**
   Follow the official GNUS.AI C++ Coding Standards in the Software Engineering Handbook (https://docs.gnus.ai/gnus.ai-gitbook/technical-information/software-engineering-handbook and the dedicated C++ Coding Standards sub-page) at all times.
   In particular:
- Use the exact naming, bracing (Allman/Ullman style), indentation, comment style, Doxygen headers, and layout rules defined there.
- All variables must be initialized.
- Always use braces on if/while/for/switch even for single statements.
- Every function and public interface must have a Doxygen-compatible header.
- Prefer Google Test + the project’s “wait condition testing templates” (condition_variable / polling patterns) in tests. NEVER use std::this_thread::sleep_for in tests.

7. **Testing discipline**
   Tests must use the project’s wait-condition templates instead of any sleep_for / sleep_until.
   Keep tests isolated, fast, and deterministic. Target ≥80 % coverage on new code.

8. **When in doubt**
   If something is missing from the project files or seems to be an older implementation from your model knowledge, ask the user for clarification before writing any code.

**Response format when the user gives a task**
- First, briefly list which files you examined.
- Then, describe the minimal change you propose (exact lines to add/modify, file names, line numbers if possible).
- Only after the user approves or gives further instructions, output the actual code diff/patch.

You are not allowed to rewrite large sections, introduce new classes, change architecture, or perform any refactoring unless explicitly requested.  
Your default mode is “tiny, surgical insertion into existing code”.

** Adhere to the Key GoF Principles for Loose Coupling
- Program to an Interface, Not an Implementation: This is the most fundamental principle for reducing coupling. By relying on abstract interfaces rather than concrete classes, components can be swapped or modified without affecting the rest of the system.
- Favor Object Composition Over Class Inheritance: Inheritance creates tight coupling between subclasses and their parents. Composition allows behavior to be combined at runtime, reducing rigid dependencies.
- Find What Varies and Encapsulate It: By encapsulating changing behavior behind a stable interface, you ensure that future changes do not break other parts of the code.

** Don't use a try and retry approach.
- Always analyze the actual codebase first, then propose a minimal change. If you need more information, ask the user before writing any code.
- As an agent, you shouldn't try to write code, execute it, and see the results.
- This means do **NOT** add debug strings in the code, then compile and run to see if they work.
    - Instead, if there is a bug, the agent should ask the user to debug the code to find the bug's root cause

## Important Guidelines
- Do not commit changes without explicit user permission.
- When I report a bug, don't start by trying to fix it. Instead, start by writing a test that reproduces the bug. Then, have subagents try to fix the bug and prove it with a passing test.
- Never commit code that you don't understand.
    - Ask the user for permission
- Never assume or speculate about something that you don't understand.
    - Interact with the user directly to understand what they're doing.
- Always run the tests before committing.
- Always run the linter before committing.
- Always run the formatter before committing.
- Always run the build before committing.
- Always run in interactive mode with the user on a step-by-step basis
- Always look in AgentDocs for other instructions.
    - The files can include SPRINT_PLAN.md, Architecture.md, CHECKPOINT.md, AGENT_MISTAKES.md
- Always look in AgentDocs for other instructions.
    - The files can include SPRINT_PLAN.md, Architecture.md, CHECKPOINT.md, AGENT_MISTAKES.md
- Always make sure to only use C++17 features and below.
    - For instance boost::coroutines only work in C++20, do NOT use it.
    - Make sure not to use other C++ versions' features above C++17
  
## Build Commands

> **See `README.md` → "Building the Project" for the full authoritative build instructions.**
> Always read README.md before attempting to build or fix build issues.

The build pattern for every platform and build type is:

```bash
cd build/<Platform>/<BuildType>   # e.g. build/OSX/Debug
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=<BuildType>
ninja
```

- `cmake ..` points to `build/<Platform>/` — **never** run cmake from the repo root
- If `CMakeCache.txt` is stale or the build directory is broken, **delete and recreate the directory**, then run cmake fresh
- The user manages thirdparty builds separately — do not attempt to build thirdparty
- Do not pass `-DTHIRDPARTY_BUILD_DIR` or other cache variables; `cmake ..` resolves everything automatically
- Build types: `Debug`, `Release`, `RelWithDebInfo`
- Platforms: `OSX`, `Linux`, `Windows`, `Android`, `iOS`

## Code Style
- Style: Based on Microsoft with modifications (see .clang-format)
- Indent: 4 spaces
- Line length: 120 characters maximum
- Classes/Methods: PascalCase
- Variables: camelCase
- Constants: ALL_CAPS
- Parentheses: space after opening and before closing: `if ( condition )`
- Braces: Each on their own line
- Error Handling: Use outcome::result<T> pattern for error propagation
- Exception Handling: **By default, generate code without exception handling. All functions should be declared noexcept unless explicitly required to throw**
- Namespaces: Use nested namespaces with full indentation
- Comments: Document interfaces and public methods
- Const-correctness: All parameter structs passed by const&
- Named initialization: Structs designed for designated initializers
- Grouped values: Related data in structs, returned by const&
- Meaningful constants: kPublicKeySize instead of magic 64
- Law of Demeter: Boost types hidden behind aliases
- Unique ownership: unique_ptr throughout, no shared_ptr
- Outcome-based errors: No exceptions in hot paths
- Always prefer const variables, const parameters, const functions. use const by default
- If possible do not use inout parameters. Only in const and return results. If needed wrapped in custom structures.
- Keep right balance in programming between object and functional programming. Prefer functional in general with less state.
- Public methods should conform to object oriented deisgn, but internal implementations can go more with functional programming patterns.
- Conform to Effective C++ book principles as close as possible
- Conform to Modern Effective C++ book principles as close as possible
- Conform to Effective STL book principles as close as possible
- Conform to API Design for C++ book principles as close as possible
- Prefer to use coroutines for high latency operations like disk io, network io, gpu work or others

## C++ Coding Rules (Based on Effective C++)

### Language Fundamentals
- Adapt your programming style based on the C++ sublanguage you're using (C, Object-Oriented C++, Template C++, STL)
- Replace #define constants with const objects or enums
- Replace function-like macros with inline functions
- Use const everywhere possible: objects, parameters, return types, and member functions
- Always initialize objects before use; prefer member initialization lists over assignments in constructor bodies
- List data members in initialization lists in the same order they're declared in the class

### Constructors, Destructors, and Assignment
- Be aware of compiler-generated special member functions (default constructor, copy constructor, copy assignment, destructor)
- Explicitly delete or declare private any compiler-generated functions you don't want
- Always declare destructors virtual in polymorphic base classes
- If a class has any virtual functions, it must have a virtual destructor
- Never allow exceptions to escape from destructors; catch and handle them internally
- Never call virtual functions during construction or destruction
- Have assignment operators return a reference to *this
- Handle self-assignment in operator= using address comparison, careful statement ordering, or copy-and-swap
- When writing copy constructors or copy assignment operators, copy all data members and all base class parts

### Resource Management
- Use RAII (Resource Acquisition Is Initialization) - acquire resources in constructors, release in destructors
- Use smart pointers (unique_ptr preferred) to manage dynamically allocated resources
- Provide explicit conversion functions to access raw resources when needed for legacy APIs
- Always match array new[] with array delete[], and scalar new with scalar delete
- Store newed objects in smart pointers in standalone statements to prevent exception-related resource leaks
- Think carefully about copying behavior for resource-managing classes (disable, reference count, or deep copy)

### Interface Design and Declarations
- Design interfaces to be easy to use correctly and hard to use incorrectly
- Use strong types, restrict operations, constrain values, and eliminate client resource management responsibilities
- Prefer pass-by-reference-to-const over pass-by-value for efficiency and to avoid slicing
- Only pass built-in types and STL iterators/function objects by value
- Never return references or pointers to local stack objects
- Never return references to heap-allocated objects that could cause memory leaks
- Declare all data members private; use getters/setters for controlled access
- Prefer non-member non-friend functions to member functions to increase encapsulation
- When type conversions should apply to all parameters (including *this), use non-member functions
- Provide a non-throwing swap member function when std::swap would be inefficient for your type

### Implementation
- Postpone variable definitions as long as possible, ideally until you have initialization values
- Minimize casting; avoid dynamic_cast in performance-sensitive code
- When casting is necessary, hide it inside a function and prefer C++-style casts
- Never return handles (references, pointers, iterators) to private object internals
- Write exception-safe code with strong or nothrow guarantees; use copy-and-swap when appropriate
- Limit inlining to small, frequently called functions; don't inline just because functions are in headers
- Minimize compilation dependencies: depend on declarations, not definitions; use Handle/Interface classes

### Inheritance and OOP
- Ensure public inheritance always models "is-a" relationships
- Never hide inherited names; use using declarations or forwarding functions to make them visible
- Pure virtual functions specify interface only
- Simple virtual functions specify interface plus a default implementation
- Non-virtual functions specify interface plus a mandatory implementation
- Consider alternatives to virtual functions: NVI idiom, Strategy pattern, function pointers
- Never redefine inherited non-virtual functions
- Never redefine inherited default parameter values (they're statically bound)
- Use composition to model "has-a" or "is-implemented-in-terms-of" relationships
- Use private inheritance rarely and only when necessary; prefer composition
- Use multiple inheritance judiciously; be aware of ambiguity and virtual base class costs

### Templates and Generic Programming
- Understand that template interfaces are implicit and based on valid expressions
- Use typename to identify nested dependent type names in templates
- Access names in templatized base classes via this->, using declarations, or explicit base class qualification
- Factor parameter-independent code out of templates to reduce code bloat
- Use member function templates to accept all compatible types
- Define non-member functions inside class templates when type conversions are needed
- Use traits classes for type information available at compile time
- Use template metaprogramming to shift work from runtime to compile-time when beneficial

### Memory Management
- Understand set_new_handler behavior for handling memory allocation failures
- Consider custom new/delete operators for performance, debugging, or usage tracking
- operator new must contain an infinite loop, call new-handler on failure, and handle zero-byte requests
- operator delete must handle null pointers safely
- Write placement delete if you write placement new to prevent memory leaks
- Class-specific new/delete should handle requests for sizes different than expected

### General Practices
- Take compiler warnings seriously; compile warning-free at maximum warning level
- Don't depend on specific compiler warnings as they vary between compilers
- Know the standard library: STL, iostreams, locales, and standard C library
- Familiarize yourself with modern C++ features and best practices

## Modern C++ Coding Rules (C++11/14/17 and Beyond)

### Type Deduction (Items 1-4)
- Understand template type deduction rules: value categories, reference collapsing, and special cases
- Understand auto type deduction: mostly follows template rules but treats braced initializers as std::initializer_list
- Understand decltype: returns exact declared type; decltype(auto) deduces from initializer using decltype rules
- Know how to view deduced types: compiler diagnostics, runtime output (typeid, Boost.TypeIndex), or IDE tooltips

### Modern Type Declarations (Items 5-6)
- Prefer auto to explicit type declarations: reduces verbosity, ensures initialization, makes refactoring easier, and avoids type mismatches
- Use explicitly typed initializer idiom when auto deduces undesired types (e.g., proxy classes like vector<bool>::reference)

### Initialization and Declarations (Items 7-10)
- Distinguish between () and {} when creating objects: braces prevent narrowing, work everywhere, but beware of std::initializer_list overloads
- Prefer nullptr to 0 and NULL: type-safe, clearer intent, works with templates, enables function overloading
- Prefer alias declarations (using) to typedefs: work with templates, more readable, support template aliases
- Prefer scoped enums to unscoped enums: no implicit conversions, namespace pollution prevention, forward declarable, can specify underlying type

### Special Member Functions (Items 11-17)
- Prefer deleted functions (= delete) to private undefined ones: better error messages, works with any function (not just members), checked at compile-time
- Declare overriding functions override: catches interface mismatches, enables better refactoring, documents intent
- Prefer const_iterators to iterators: const-correctness, C++11 makes them practical with cbegin/cend
- Declare functions noexcept if they won't emit exceptions: enables optimizations (especially for move operations), required for some STL containers
- Use constexpr whenever possible: computed at compile-time, usable in constant expressions, broader scope than const
- Make const member functions thread-safe: use mutex for mutable data, consider std::atomic for simple cases
- Understand special member function generation: default constructor, destructor, copy ops, move ops; generation rules depend on what you declare

### Smart Pointers (Items 18-22)
- Use std::unique_ptr for exclusive-ownership resource management: zero overhead, move-only, perfect for factories, supports custom deleters
- Use std::shared_ptr for shared-ownership resource management: reference counted, thread-safe, larger overhead, use make_shared
- Use std::weak_ptr for std::shared_ptr-like pointers that can dangle: breaks reference cycles, cache implementations, observer patterns
- Prefer std::make_unique and std::make_shared to direct use of new: exception safety, efficiency (one allocation for shared_ptr), conciseness
- When using Pimpl Idiom, define special member functions in implementation file: required for unique_ptr with incomplete types

### Move Semantics and Perfect Forwarding (Items 23-30)
- Understand std::move and std::forward: move is unconditional cast to rvalue; forward is conditional cast preserving value category
- Distinguish universal references (T&&) from rvalue references: T&& is universal only with type deduction; rvalue reference otherwise
- Use std::move on rvalue references, std::forward on universal references: move enables moving; forward preserves lvalue/rvalue-ness
- Avoid overloading on universal references: they're too greedy and hijack overload resolution
- Familiarize yourself with alternatives to overloading on universal references: tag dispatch, enable_if, pass by value, trade-offs
- Understand reference collapsing: & + & = &, && + anything = that thing (except && + && = &&)
- Assume move operations are not present, not cheap, and not used: move isn't always generated, might not be faster, may not be called
- Familiarize yourself with perfect forwarding failure cases: braced initializers, null/0 pointers, declaration-only names, overloaded function names, bitfields

### Lambda Expressions (Items 31-34)
- Avoid default capture modes: [=] risks dangling pointers (especially to this); [&] risks dangling references and leads to lifetime issues
- Use init capture to move objects into closures: enables move-only types in captures, more efficient than copy capture
- Use decltype on auto&& parameters to std::forward them: enables generic lambdas to perfectly forward parameters
- Prefer lambdas to std::bind: more readable, easier to optimize, works better with overloading and templates

### Concurrency API (Items 35-40)
- Prefer task-based programming (std::async) to thread-based: automatic thread management, handles exceptions, returns futures
- Specify std::launch::async if asynchronicity is essential: default policy may run synchronously
- Make std::threads unjoinable on all paths: use RAII wrappers, join or detach before destruction to avoid termination
- Be aware of varying thread handle destructor behavior: std::thread destructs differently than std::future
- Consider void futures for one-shot event communication: condition variables for multiple waiters
- Use std::atomic for concurrency, volatile for special memory: atomic for thread-safe operations, volatile for hardware/signal handlers

### Tweaks and Best Practices (Items 41-42)
- Consider pass by value for copyable parameters that are cheap to move and always copied: one less copy, cleaner code
- Consider emplacement (emplace_back, emplace) instead of insertion: constructs in-place, avoids temporaries, more efficient

## Testing Practice
- Unit tests should be placed in the test/ directory matching source structure
- Use cmake test framework for unit tests
- Test names should be descriptive of what they're testing

## Debugging Workflow

When something is not working as expected, follow this order:

1. **Check for existing tests first.** Is there already a unit test covering the function or component that's not working? If so, does the test pass? If the test passes but the code doesn't work in practice, the test has a coverage gap.

2. **Write a test if none exists.** If there is no test for the problematic function, write one *before* attempting any fix. The test must cover both:
   - **Happy path** — the function working correctly under normal inputs
   - **Unhappy path** — edge cases, invalid inputs, error conditions

   Use the project's wait-condition templates (no `std::this_thread::sleep_for` in tests). The test IS the specification — it defines what correct behavior looks like.

3. **Trace the code to find the issue.** Only after a test exists (and fails in the expected way), trace through the code — follow call chains, check invariants, verify assumptions — to identify the root cause. Use `spdlog::debug()` for diagnostic output (never `fprintf`/`cout`/`printf`), enabled with `--debug` at runtime.

4. **Fix the root cause, not the symptom.** Once the root cause is identified, make the minimal fix. The test written in step 2 should now pass. If it doesn't, the fix is incomplete or the root cause was misidentified — go back to step 3.

5. **Ask the user for help if stuck.** If tracing doesn't reveal the issue, ask the user to help debug rather than guessing or trying random changes.