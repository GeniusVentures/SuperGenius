# Stack Research — GeniusNode Construction Refactor

**Date:** 2026-07-02
**Scope:** C++17 idioms for collapsing overloaded factories into a `std::variant`-driven factory + moving runtime knobs into RapidJSON config files.

> The existing system stack is fully mapped in `.planning/codebase/STACK.md`. This doc covers ONLY the patterns needed for the refactor.

## Recommendation Summary

| Need | Recommendation | Confidence |
|------|----------------|------------|
| Account-source dispatch | `std::variant` + `std::visit` with a lambda-overload set | High |
| Variant alternatives | Named aggregate structs (not raw tagged values) | High |
| JSON config read | Existing RapidJSON `HasMember`+`IsXxx`+`GetXxx` pattern (already in codebase) | High |
| String → enum parse | Free function `NodeTypeFromString()` with fallback | High |
| Bool → enum at boundary | Keep derived `is_full_node_` bool; introduce `NodeType` enum as source of truth | High |

## std::variant Factory Dispatch (C++17)

The codebase is `CMAKE_CXX_STANDARD 17` (`build/CommonCompilerOptions.cmake`). Use std::variant — do **not** introduce inheritance hierarchies or `std::any`.

### Variant alternatives — use named structs, not raw values

```cpp
// In a small header, e.g. src/account/AccountSource.hpp
namespace sgns
{
    struct NewAccount        {};                       // generate fresh identity
    struct FromPrivateKey    { std::string key_hex; }; // eth private key
    struct FromMnemonic      { std::string mnemonic; };
    struct FromPublicKey     { std::string address; }; // watch-only

    using AccountSource = std::variant<NewAccount, FromPrivateKey, FromMnemonic, FromPublicKey>;
}
```

**Why named structs:** self-documenting at call sites (`FromPrivateKey{key}` reads better than positional args), each alternative carries its own payload type-safely, and adding a future source is a non-breaking variant extension. Do **not** use a single struct with a tag enum + optional fields (defeats the variant's exhaustiveness guarantee).

### Dispatch with std::visit + overload set

```cpp
std::shared_ptr<GeniusAccount> account = std::visit(
    [this](auto &&src) -> std::shared_ptr<GeniusAccount> {
        using T = std::decay_t<decltype(src)>;
        if constexpr (std::is_same_v<T, NewAccount>)
            return GeniusAccount::New(token_id, write_path, is_full_node_);
        else if constexpr (std::is_same_v<T, FromPrivateKey>)
            return GeniusAccount::NewFromPrivateKey(token_id, src.key_hex.c_str(), write_path, is_full_node_);
        else if constexpr (std::is_same_v<T, FromMnemonic>)
            return GeniusAccount::NewFromMnemonic(token_id, src.mnemonic, write_path, is_full_node_);
        else if constexpr (std::is_same_v<T, FromPublicKey>)
            return GeniusAccount::NewFromPublicKey(token_id, src.address, is_full_node_);
    },
    source );
```

**Why this over a switch on `index()`:** `if constexpr` chains are exhaustive when all alternatives are handled, and adding a variant member without handling it produces a compile warning under `-Wswitch`-style checks. Prefer a small lambda-overload helper if the codebase already has one; if not, the `if constexpr` chain above is sufficient and avoids a new utility.

**Key std facilities (all C++17):** `std::variant`, `std::visit`, `std::is_same_v`, `std::decay_t`, `if constexpr`. Avoid `std::get<T>` (throws `std::bad_variant_access`) in favor of `std::holds_alternative`/`std::get_if` only if not using `visit`.

## RapidJSON Config Reading — match existing pattern

The codebase already parses both config files identically with RapidJSON. Match it exactly (see `GeniusNode.cpp:273-311` for `LoadSgnsConfig`, `GeniusNode.cpp:791-832` for `InitNetwork`). The pattern:

```cpp
// Defaults declared first
uint16_t base_port = 40001;       // current default arg
bool     autodht   = true;        // current default arg

// Then override from config if present + correctly typed
if ( config_json.HasMember( "base_port" ) && config_json["base_port"].IsUint() )
{
    base_port = static_cast<uint16_t>( config_json["base_port"].GetUint() );
    node_logger_->info( "network_config.json: base_port={}", base_port );
}
```

**Field type choice for ports:** the existing `pubsub_port` is stored as a **string** (`IsString()` → `std::stoi`, see `GeniusNode.cpp:791-805`). For `base_port`, prefer a numeric field (`IsUint()`) — string-to-int parsing for `pubsub_port` is fragile (the code wraps `std::stoi` in try/catch). New keys should not inherit that wart. Document the divergence in a comment.

**Default-on-missing-key is mandatory:** deployed config files lack the new keys. Every new read must follow the existing `HasMember && IsXxx` guard and keep the pre-declared default when absent. Never hard-fail on a missing key.

## NodeType Enum + String Parsing

### Enum location

```cpp
// In src/account/GeniusNode.hpp (alongside the existing NodeState/Error enums at lines 129/143)
enum class NodeType : uint8_t
{
    Light   = 0,   // default; is_full_node_ = false
    Full    = 1,   // is_full_node_ = true
    Archive = 2    // is_full_node_ = true (forward-compat; same behavior as Full today)
};
```

**Order rationale:** `Light = 0` so the zero-initialized default (value 0) maps to the safe non-full behavior, matching today's `is_full_node = false` default.

### String → enum helper

```cpp
inline NodeType NodeTypeFromString( std::string_view s )
{
    if ( s == "Full" )    return NodeType::Full;
    if ( s == "Archive" ) return NodeType::Archive;
    // anything else (including "Light", "", or absent) → Light
    return NodeType::Light;
}
```

Log the parsed value and the resolved derived bool at INFO so operators can verify their config took effect (matches existing `"sgns_config.json: is_processor={}"` logging style at `GeniusNode.cpp:282`).

## What NOT to Use

| Avoid | Why |
|-------|-----|
| `std::any` | Type-erased, no compile-time exhaustiveness; variant is strictly better for a closed set |
| Inheritance hierarchy (`IAccountSource` + subclasses) | Heap allocation + virtual dispatch for 4 trivial alternatives is overkill; variant is stack-allocated and value-semantic |
| Boost.Program_options / a config framework | The codebase uses JSON files; stay in RapidJSON |
| Tagged struct (`struct AccountSource{ enum Tag; string key; string mnemonic; ... }`) | Loses exhaustiveness, invites "set the wrong field" bugs |
| `std::get<T>` without prior check | Throws; prefer `std::visit` or `std::get_if` |
| Changing `pubsub_port`'s string format in this milestone | Out of scope; only add new keys |

## Conventions to Match (from `.planning/codebase/CONVENTIONS.md`)

- Private members: `snake_case_` trailing underscore (e.g. `is_full_node_`, `autodht_`)
- Factory returns `std::shared_ptr<T>` (existing pattern)
- Doxygen `@param`/`@brief` on every public declaration
- RapidJSON `HasMember` + typed `IsXxx` guard before every `GetXxx`
- INFO log every config value resolved (with the file name prefix)
