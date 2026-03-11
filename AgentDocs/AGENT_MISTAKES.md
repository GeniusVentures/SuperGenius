# Agent Mistakes Log

Mistakes made by LLM agents during C++ development. Each entry describes what went wrong, why, and the rule to follow. Consult this file **before** writing any code.

**Core principle behind most entries**: The agent wrote code based on assumptions about how something worked instead of reading the actual source first.

---

## TOOL USAGE

### M001 — `replace_string_in_file` leaving duplicate function bodies
**What happened**: Matched only the opening line of a function in the search string. The tool replaced that line but left the old function body intact below it, producing a duplicate and compile errors. Happened multiple times on the same file in the same session even after being flagged.  
**Rule**: When replacing a function body, the search string must include enough of the body to uniquely identify AND fully consume the old content. If the function is large, replace the whole file. **After any replace on a `.cpp` file, immediately read back the region around the closing `} // namespace` brace to verify no duplicate tail remains.**

### M002 — Reformatting code unnecessarily
**What happened**: When rewriting a file, changed ~37 formatting decisions (brace placement, single-line if statements, spacing) that differed from the project's existing style, causing the user to have to manually revert them.  
**Rule**: Never reformat code that isn't part of the task. Match the existing file's style exactly. Only change lines that are necessary for the task. If in doubt, copy the surrounding style character-for-character.

---

## C++ LANGUAGE / LIBRARY

### M003 — Raw pointer into `std::vector` captured in a lambda
**What happened**: Captured `T* ptr = &vec.back()` in a lambda. A subsequent `vec.push_back()` reallocated the vector, invalidating `ptr` and causing undefined behaviour.  
**Rule**: Never capture a pointer or reference to a `std::vector` element in a lambda or callback that outlives the current scope. Capture a stable key (an id, a copy of the value, etc.) and look it up at call time.

### M004 — Calling `.message()` on a plain enum error type
**What happened**: Added diagnostic code calling `.message()` on an error value. The error type was a plain `enum class`, not a `std::error_code`, so it had no such method and failed to compile.  
**Rule**: Before calling any method on an error type, read its definition. Plain enums have no methods — use `static_cast<int>()` or a project-provided `to_string()`. Only `std::error_code` / `std::error_condition` have `.message()`.

### M005 — Using an RLP compact-encoding helper to decode a fixed-width ABI value
**What happened**: Used a helper designed for RLP's variable-length compact encoding (`from_big_compact`) to decode a 32-byte ABI word. The helper rejects leading zero bytes (an RLP canonicality rule), so small values with many leading zeros decoded as zero.  
**Rule**: Understand what encoding a helper was designed for before using it. ABI encoding uses fixed 32-byte words with leading zeros — a completely different contract from RLP's compact encoding. Use the right tool for each encoding format; do not cross them.

### M006 — Using a constructor or API without reading its actual signature
**What happened**: Attempted to construct `intx::uint256{lo128, hi128}` — no such constructor exists. Multiple further attempts with wrong argument counts before finally reading the header.  
**Rule**: **Read the actual header/source before writing a non-trivial construction or API call.** Do not guess constructors, method signatures, or template parameters. One read of the relevant file would have prevented three failed attempts. This is the most common root cause of wasted iterations.

### M007 — Assuming a return type has a standard interface without checking
**What happened**: Called `.begin()` / `.end()` directly on the return value of a crypto library hash function. The actual return type was an internal proxy object with no iterator interface.  
**Rule**: When using a third-party or unfamiliar library, read how the return type is actually used elsewhere in the codebase before writing new code against it. Find an existing usage example first, then follow that pattern exactly.

---

## DESIGN / LOGIC

### M008 — Re-broadcasting a filtered callback to all subscribers
**What happened**: An inner component (`EventWatcher`) already applied address and topic filters and called a single specific callback. That callback then looped over all subscriptions and re-dispatched to any whose signature hash matched — discarding the address filter the inner component had already applied. Two subscribers watching different contracts both fired for every log.  
**Rule**: When an inner component already does filtering and invokes a specific callback, that callback must act on behalf of exactly one subscription. Do not re-broadcast inside the callback — doing so throws away the filter results computed by the inner component.

---

## PROCESS

### P001 — Trying to observe runtime behaviour by adding debug prints, compiling, and running
**Rule**: Do not add debug strings to source, compile, and run to observe behaviour. This is a "try and see" loop that wastes build cycles. Instead: read the relevant source files, trace the logic statically, identify the root cause, then make a single targeted fix.

### P002 — Searching for things that are already known from the project structure
**Rule**: The project file tree is provided at the start of each session. Do not search for files or symbols that are already visible in the tree. Open and read the relevant file directly.

---

## DOCUMENTATION

### M009 — Stripping Doxygen comments during file rewrites
**What happened**: When rewriting a header file, replaced a fully documented block with a stripped-down version that omitted `@param`, `@return`, and descriptive Doxygen comments that were present in the original.  
**Root cause**: The rewrite focused on the code changes and did not preserve the existing documentation.  
**Rule**: When editing or rewriting any function declaration, always carry forward all existing Doxygen comments verbatim. Only add or modify comments that are directly related to the change being made. Never silently drop `@brief`, `@param`, `@return`, or `@note` tags.

### M010 — Misuse of `[[nodiscard]]` on side-effect functions
**What happened**: `mark_seen()` was declared `[[nodiscard]]` even though its primary purpose is the side effect of recording a block. This produced 17 warnings at call sites where the return value is intentionally unused (setup code in tests, production dispatch loops).  
**Root cause**: `[[nodiscard]]` was applied mechanically without considering whether the return value is the *primary* reason to call the function.  
**Rule**: Only apply `[[nodiscard]]` to functions whose *sole or primary* purpose is to produce a return value (factory functions, pure queries, error-returning operations). Do NOT apply it to functions whose primary purpose is a side effect and whose return value is merely a convenience indicator. When in doubt: if the function name is a verb acting on state (`mark_*`, `set_*`, `push_*`, `process_*`), do not use `[[nodiscard]]`.

### M011 — Chained string if/else-if instead of a map dispatch
**What happened**: Chain name → bootnode and chain name → network_id/genesis_hash lookups were written as long chains of `if (x == "a") ... else if (x == "b") ...`, one branch per chain. Adding a new chain required touching multiple blocks in multiple places.  
**Root cause**: Writing the obvious imperative translation instead of recognising the pattern as a table lookup.  
**Rule**: Any function whose entire body is `if (key == "a") return A; if (key == "b") return B; ...` is a map lookup. Use `std::unordered_map<std::string, Value>` (static const inside the function) with a single `.find()` call. Every valid key gets its own entry — do not add alias entries or normalisation hacks; if a name was never advertised, don't support it. This applies to chain names, message type strings, command names, and any other string-keyed dispatch.

### M012 — Magic numbers in code
**What happened**: Raw numeric literals (`98`, `97`, `65`, `32`, `1`) were used directly in packet parsing and construction code with only inline comments explaining their meaning. Even `kPacketHeaderSize = kHashSize + kSigSize + 1` left the `1` unexplained.  
**Root cause**: Writing the first obvious thing instead of naming every distinct quantity.  
**Rule**: Every numeric literal that represents a domain concept must be a named `constexpr`. Arithmetic combining named constants is fine (`kHashSize + kSigSize + kPacketTypeSize`), but a bare literal in that arithmetic is not — give it a name too. No exceptions for "obvious" sizes like 4 (IPv4), 64 (pubkey), or 1 (type byte).

### M013 — `co_spawn(..., detached)` with a coroutine returning `Result<T>`
**What happened**: `co_spawn(io, find_node(...), detached)` and `co_spawn(io, ping(...), detached)` both failed to compile because `boost::asio::detached` requires the coroutine return type to be default-constructible, but `Result<T>` (`basic_result`) has its default constructor explicitly deleted.  
**Root cause**: Assuming any awaitable can be passed directly to `co_spawn(..., detached)`.  
**Rule**: `co_spawn(..., detached)` only works with `awaitable<void>`. If a coroutine returns `awaitable<Result<T>>` or any other non-default-constructible type, wrap it in a void lambda coroutine that discards the result:
```cpp
asio::co_spawn(io,
    [this, ip, port]() -> boost::asio::awaitable<void>
    {
        auto result = co_await some_coroutine(ip, port);
        (void)result;
    },
    asio::detached);
```
Never pass a `Result`-returning coroutine directly to `co_spawn(..., detached)`.

### M014 — Using bare integer literals instead of `sizeof()` on wire structs for protocol sizes
**What happened**: Constants like `kFrameHeaderSize = 16`, `kMacSize = 16`, `kAesBlockSize = 16`, `kUncompressedPubKeySize = 65` were written as raw integer literals. When checking sizes or declaring arrays, the same literals were repeated directly in code (e.g. `std::array<uint8_t, 65>`, `constexpr size_t kMinSize = 65 + 16 + 32`).  
**Root cause**: Writing the first obvious number instead of thinking about what the number *represents* structurally.  
**Rule**: Any size constant that corresponds to an actual on-wire struct layout must be expressed as `sizeof(WireStruct)`. Define a minimal POD struct whose layout exactly matches the wire format, then derive the constant:
```cpp
struct FrameHeaderWire { uint8_t bytes[16]; };
inline constexpr size_t kFrameHeaderSize = sizeof(FrameHeaderWire);
```
Derived sizes must be computed from the named constants, never repeated as raw literals:
```cpp
// WRONG
constexpr size_t kMinSize = 65 + 16 + 32;
// RIGHT
constexpr size_t kMinSize = kUncompressedPubKeySize + kAesBlockSize + kHmacSha256Size;
```
This rule applies to all protocol-layer size arithmetic. The only numbers that may appear as raw literals are truly dimensionless multipliers (e.g. the `3` in `kFrameLengthSize = 3` is a wire-spec constant that belongs in a named `constexpr`, not a `sizeof`, because no struct field maps to it).

### M015 — Using constants without namespace qualification in a header outside that namespace
**What happened**: Added `#include <discv4/discv4_constants.hpp>` to `discovery.hpp` and used `kWireHashSize`, `kWireSigSize`, `kWirePacketTypeSize` unqualified. The constants are defined inside `namespace discv4`, but the code in `discovery.hpp` is at global scope — so the names were not found and the build failed with 7 "undeclared identifier" errors.  
**Root cause**: Added the include without checking the namespace context of the call site. Assumed the include alone was sufficient.  
**Rule**: Before using any constant or type from an included header, check two things:
1. What namespace is it declared in?
2. What namespace is the call site in?

If they differ, either qualify every use (`discv4::kWireHashSize`) or add a scoped `using namespace discv4;` / `using discv4::kWireHashString;` at the narrowest possible scope. Never assume an include brings names into the current namespace without qualification.

### M016 — Debugging a crypto protocol by running against live peers instead of test vectors
**What happened**: When the RLPx handshake failed (MAC mismatch, auth rejected), the agent attempted to diagnose by adding logging, rebuilding, and running against live Ethereum nodes. This produced ambiguous network-level failures (timeout, connection dropped, MAC mismatch) that changed every run depending on which peer answered.  
**Root cause**: No unit test existed that verified the key derivation against known-good values. The agent tried to use the live network as an oracle.  
**Rule**: Crypto protocol implementations must be verified against **published test vectors** before any live network testing. When a handshake fails:
1. Find the reference implementation's test vectors (go-ethereum has `TestHandshakeForwardCompatibility` in `p2p/rlpx/rlpx_test.go` with hardcoded keys, nonces, wire bytes, and expected AES/MAC secrets).
2. Write a unit test that feeds those exact inputs into the key derivation function and asserts the exact expected outputs.
3. Only after the unit test passes should live network testing begin.

Live network testing cannot distinguish between "our crypto is wrong" and "the peer is offline/incompatible/behind a firewall". A deterministic test vector can.

### M017 — Using plain `//` comments for declarations in header files
**What happened**: New function declarations were added to header files with plain `// comment` style instead of Doxygen `///` or `/** */` blocks.  
**Root cause**: Writing the first obvious thing without checking the documentation rule in `CLAUDE.md`.  
**Rule**: Every function declaration (and struct/class member) in a header file **must** use Doxygen-style comments:
- `@brief` / `@param` / `@return` / `@note` tags, or the triple-slash `///` shorthand.
- Plain `//` comments are only acceptable inside `.cpp` implementation files for inline clarifications.
- When adding a new declaration, always look at the surrounding declarations in the same header and match their Doxygen style exactly.
- Never silently omit `@param` or `@return` for non-trivial declarations.

### M018 — Using `std::cout` / `std::cerr` for debug output instead of spdlog
**What happened**: When a debug print was needed, `std::cerr` was inserted directly into source code, requiring an `#include <iostream>` and a build cycle to observe behaviour.  
**Root cause**: Reaching for the obvious C++ I/O stream instead of using the project's established logging system.  
**Rule**: **Never use `std::cout` or `std::cerr` for debug output.** Use the project spdlog system exclusively:
1. In each `.cpp` file that needs logging, declare a function-local static logger at the top of the relevant function (matching the pattern used everywhere else):
   ```cpp
   static auto log = rlp::base::createLogger("subsystem.component");
   ```
2. Use `SPDLOG_LOGGER_DEBUG(log, "msg {}", val)`, `SPDLOG_LOGGER_INFO(...)`, `SPDLOG_LOGGER_WARN(...)`, `SPDLOG_LOGGER_ERROR(...)`.
3. The global spdlog level is already controlled by the `--log-level` CLI flag in `eth_watch` (and similar entry points). Setting `--log-level debug` will show all `DEBUG` output with zero code changes.
4. `std::cout` is only acceptable for **user-facing program output** (e.g., final results printed to the terminal by design). It is never acceptable for diagnostic or debug output.

