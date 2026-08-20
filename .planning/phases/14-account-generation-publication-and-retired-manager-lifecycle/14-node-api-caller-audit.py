#!/usr/bin/env python3
"""Declaration-driven Phase 14 account-bound GeniusNode caller manifest.

The manifest intentionally describes migration ownership only; it never rewrites a
caller.  Compile commands are used to expand a source expression to every CMake
target that builds that source, including shared headers through their consumers.
"""
import argparse
import csv
import json
import pathlib
import re
import sys
from collections import defaultdict

ROOT = pathlib.Path(__file__).resolve().parents[3]
HEADER = ROOT / "src/account/GeniusNode.hpp"
METHODS = (
    "SelectAccount TransferAccount DeleteAccount MergeAccount SetPayoutAddress "
    "MintTokens ProcessImage GetBalance GetInTransactions GetOutTransactions "
    "CountTransactions GetAddress GetMnemonicOfActiveAccount GetProcessingStatus "
    "TransferFunds PayDev WaitForFinalized IsFinalized WaitForTransactionOutgoing "
    "WaitForTransactionIncoming WaitForEscrowRelease GetTransactionManagerState "
    "GetTransactionManager GetTransactionStatus ConfigureRpcEndpoint StartProcessing "
    "StopProcessing ResetProcessingMembers SendTransactionAndProof"
).split()
# A caller that has already moved to the checked account-address API still
# implements the GetAddress contract.  Keep its concrete expression in the
# manifest so the audit proves that a fixture no longer reaches the temporary
# default-returning shim.
CALL_METHODS = {method: method for method in METHODS}
CALL_METHODS["GetActiveAccountAddress"] = "GetAddress"
CALL_METHODS["GetActiveProcessingStatus"] = "GetProcessingStatus"
ALLOWED = {"14-06", "14-07", "14-08", "14-09", "14-10", "14-11", "14-12"}
FIXTURE_SOURCES = {
    "test/src/account/account_management_test.cpp",
    "test/src/account/network_config_precedence_test.cpp",
    "test/src/account/node_type_derivation_test.cpp",
    "test/src/blockchain/node_startup_test.cpp",
    "test/src/blockchain/blockchain_genesis_test.cpp",
}

# Plan 14-08 owns the processing/transaction-sync caller partition below. Other
# processing consumers remain staged for their separately planned migrations.
PLAN_14_08_SOURCES = {
    "test/src/processing_nodes/processing_nodes_test.cpp",
    "test/src/processing_nodes/full_node_test.cpp",
    "test/src/processing_nodes/child_tokens_test.cpp",
    "test/src/transaction_sync/genius_node_bootstrap_reconnect_test.cpp",
    "test/src/transaction_sync/transaction_sync_test.cpp",
    "test/src/transaction_sync/transaction_crash_test.cpp",
    "test/src/transaction_sync/migration_sync_test.cpp",
}

# Plan 14-09 owns only the multi-account caller partition below. Keep its
# migration proof independent from every later caller partition so the exact
# two-target build and manifest rows remain auditable on their own.
PLAN_14_09_SOURCES = {
    "test/src/multiaccount/multi_account_sync.cpp",
    "test/src/multiaccount/policy_lifetime_multi_account_test.cpp",
}

# Bridge E2E callers form their own source/target partition.  The bridge-race
# fixtures remain exclusively with Plan 14-11 and must not satisfy 14-10 proof.
PLAN_14_10_SOURCES = {
    "test/src/bridge_e2e/bridge_e2e_test.cpp",
    "test/src/bridge_e2e/bridge_anvil_e2e_test.cpp",
    "test/src/bridge_e2e/bridge_anvil_catchup_e2e_test.cpp",
    "test/src/bridge_e2e/bridge_sepolia_e2e_test.cpp",
    "test/src/bridge_e2e/bridge_rlpx_e2e_test.cpp",
}

# The Plan 14-12 processing-multi target is defined in its child CMake file but
# is not yet part of the parent test graph while this audit first runs.  Keep
# that known local target definition as evidence instead of collapsing the row
# to ``header-consumer`` before Task 2 wires the parent subdirectory.
PLAN_14_12_TARGETS = {
    "example/node_test/NodeExample.cpp": {"node_example"},
    "test/src/processing_multi/processing_multi_test.cpp": {"processing_multi_test"},
}

# `bridge_race_fixture.hpp` is included by five independently-built race targets.
# Headers do not have compile_commands entries of their own, so model their concrete
# consumers explicitly instead of collapsing their lifecycle calls into the generic
# `header-consumer` placeholder.  This expansion is a precondition for removing the
# temporary GeniusNode compatibility shims in the final caller-closure plan.
BRIDGE_RACE_FIXTURE = "test/src/bridge_race/bridge_race_fixture.hpp"
BRIDGE_RACE_TARGETS = {
    "bridge_race_single_burn_test",
    "bridge_race_batch_test",
    "bridge_race_fault_rpc_test",
    "bridge_race_fault_kill_test",
    "bridge_race_fault_partition_test",
}

def owner_for(source):
    s = source.replace("\\", "/")
    if s in {"src/account/GeniusNode.cpp", "src/account/GeniusNode.hpp", "test/src/node/node_initialization_progress.cpp"}:
        return "14-06"
    if "/bridge_race/" in s:
        return "14-11"
    if s in PLAN_14_10_SOURCES:
        return "14-10"
    if "/multiaccount/" in s:
        return "14-09"
    if "/processing_multi/" in s or "/NodeExample.cpp" in s:
        return "14-12"
    if "/processing" in s or "/transaction_sync" in s:
        return "14-08"
    return "14-07"

def contract_for(method, source):
    if method in {"SelectAccount", "TransferAccount", "DeleteAccount", "MergeAccount"}:
        return "lifecycle-selection"
    if method == "ConfigureRpcEndpoint" or (
        source == "test/src/node/node_initialization_progress.cpp" and method == "GetAddress"
    ):
        return "configured-bootstrap"
    return "active-ready"

def identity_for(contract):
    return "configured-bootstrap" if contract == "configured-bootstrap" else "active-generation"

def targets_by_source(commands):
    result = defaultdict(set)
    if not commands.exists():
        return result
    for entry in json.loads(commands.read_text()):
        source = pathlib.Path(entry.get("file", ""))
        try:
            rel = source.resolve().relative_to(ROOT).as_posix()
        except ValueError:
            continue
        cmd = entry.get("command", "")
        match = re.search(r"CMakeFiles/([^/]+)\.dir", cmd)
        if match:
            result[rel].add(match.group(1))
    return result

def discover_sources():
    for directory in (ROOT / "src", ROOT / "test", ROOT / "example"):
        if not directory.exists():
            continue
        for path in directory.rglob("*"):
            if path.suffix in {".cpp", ".cc", ".cxx", ".hpp", ".h"}:
                yield path

def strip_comments_and_strings(text):
    """Replace C++ comments and literals without mistaking URL text for comments."""
    output = []
    index = 0
    length = len(text)
    while index < length:
        if text.startswith("//", index):
            end = text.find("\n", index)
            if end == -1:
                break
            output.append("\n")
            index = end + 1
        elif text.startswith("/*", index):
            end = text.find("*/", index + 2)
            end = length if end == -1 else end + 2
            output.append("\n" * text[index:end].count("\n"))
            index = end
        elif text.startswith('R"', index):
            delimiter_end = text.find("(", index + 2)
            if delimiter_end == -1:
                output.append(text[index])
                index += 1
                continue
            delimiter = text[index + 2:delimiter_end]
            terminator = ")" + delimiter + '"'
            end = text.find(terminator, delimiter_end + 1)
            end = length if end == -1 else end + len(terminator)
            output.append("\n" * text[index:end].count("\n"))
            index = end
        elif text[index] in {'"', "'"}:
            quote = text[index]
            end = index + 1
            while end < length:
                if text[end] == "\\\\":
                    end += 2
                elif text[end] == quote:
                    end += 1
                    break
                else:
                    end += 1
            output.append("\n" * text[index:end].count("\n"))
            index = end
        else:
            output.append(text[index])
            index += 1
    return "".join(output)

def genius_node_receivers(text):
    """Return simple receiver expressions proved to have GeniusNode type.

    This audit intentionally stays declaration-driven rather than treating a
    method spelling as proof of a receiver type.  The API shares several names
    (notably GetAddress and GetBalance) with accounts, UTXO managers, and
    TransactionManager.  Recognise only local/member variables declared as a
    GeniusNode smart pointer/reference/value, plus ``auto`` values constructed
    directly by GeniusNode::New.  The resulting set is deliberately small and
    deterministic; callers using another abstraction must expose a real
    GeniusNode declaration before they enter this migration manifest.
    """
    qualified = r"(?:sgns::)?GeniusNode"
    receivers = set()
    pointer = re.compile(
        rf"(?:\bconst\s+)?std::(?:shared_ptr|unique_ptr)\s*<\s*(?:const\s+)?{qualified}\s*>"
        rf"\s*(?:const\s*)?[&*]?\s*([A-Za-z_]\w*)"
    )
    direct = re.compile(rf"\b(?:const\s+)?{qualified}\s*[&*]?\s*([A-Za-z_]\w*)")
    constructed = re.compile(
        rf"\bauto\s+(?:const\s+)?([A-Za-z_]\w*)\s*=\s*(?:{qualified})::New\s*\("
    )
    for pattern in (pointer, direct, constructed):
        receivers.update(pattern.findall(text))
    return receivers

def genius_node_calls(text, receivers, include_definitions=False):
    """Find only calls whose receiver is a proved GeniusNode expression."""
    methods = "|".join(CALL_METHODS)
    call = re.compile(
        rf"\b(?P<receiver>[A-Za-z_]\w*(?:\s*\.\s*[A-Za-z_]\w*)?)\s*"
        rf"(?P<operator>->|\.)\s*(?P<method>{methods})\s*\("
    )
    found = set()
    for match in call.finditer(text):
        receiver = re.sub(r"\s+", "", match.group("receiver"))
        if receiver in receivers:
            found.add(match.group("method"))
    if include_definitions:
        definitions = re.compile(rf"\bGeniusNode::(?P<method>{methods})\s*\(")
        found.update(match.group("method") for match in definitions.finditer(text))
    return found

def check_receiver_self_test():
    """Keep the audit's type and lexer boundary from silently regressing."""
    sample = r'''
        std::shared_ptr<sgns::GeniusNode> node;
        const std::shared_ptr<GeniusNode> &node_ref = node;
        node->GetBalance();
        node_ref.GetAddress();
        // node->GetBalance();
        const char *text = "node->GetBalance()";
        account->GetAddress();
        manager.CountTransactions();
        helper.GetBalance();
    '''
    calls = genius_node_calls(strip_comments_and_strings(sample), genius_node_receivers(sample))
    if calls != {"GetBalance", "GetAddress"}:
        raise SystemExit("receiver self-check failed: real GeniusNode calls were lost or false positives accepted")

def rows(commands):
    targets = targets_by_source(commands)
    rows = []
    for method in METHODS:
        contract = contract_for(method, "src/account/GeniusNode.hpp")
        rows.append({"method": method, "expression": f"declaration:{method}", "contract": contract,
                     "identity_kind": identity_for(contract), "source": "src/account/GeniusNode.hpp",
                     "targets": ";".join(sorted(targets.get("src/account/GeniusNode.hpp", {"genius_node_test"}))),
                     "owner_plan": "14-06", "disposition": "migrate"})
    for path in discover_sources():
        rel = path.relative_to(ROOT).as_posix()
        if rel == "src/account/GeniusNode.hpp":
            continue
        raw_text = path.read_text(errors="ignore")
        text = strip_comments_and_strings(raw_text)
        found = sorted(genius_node_calls(
            text, genius_node_receivers(text), include_definitions=rel == "src/account/GeniusNode.cpp"))
        for called_method in found:
            method = CALL_METHODS[called_method]
            # The shared bridge-race fixture owns a separately constructed account
            # identity during pre-ready bootstrap; all other fixture node calls use
            # the checked active-generation surface.
            contract = ("configured-bootstrap" if rel == BRIDGE_RACE_FIXTURE and called_method == "GetAddress"
                        else contract_for(method, rel))
            row_targets = (
                BRIDGE_RACE_TARGETS
                if rel == BRIDGE_RACE_FIXTURE
                else PLAN_14_12_TARGETS.get(rel, targets.get(rel, {"header-consumer"}))
            )
            rows.append({"method": method, "expression": f"{rel}:{called_method}", "contract": contract,
                         "identity_kind": identity_for(contract), "source": rel,
                         "targets": ";".join(sorted(row_targets)),
                         "owner_plan": owner_for(rel), "disposition": "migrate"})
        if rel in FIXTURE_SOURCES:
            rows.append({"method": "GetAddress", "expression": f"{rel}:fixture-configured-bootstrap",
                         "contract": "configured-bootstrap", "identity_kind": "configured-bootstrap", "source": rel,
                         "targets": ";".join(sorted(targets.get(rel, {"header-consumer"}))),
                         "owner_plan": owner_for(rel), "disposition": "migrate"})
    return sorted(rows, key=lambda row: (row["method"], row["expression"], row["targets"]))

FIELDS = ["method", "expression", "contract", "identity_kind", "source", "targets", "owner_plan", "disposition"]

def check(rows, owners):
    errors = []
    listed = {row["method"] for row in rows}
    missing = sorted(set(METHODS) - listed)
    if missing: errors.append("missing methods: " + ", ".join(missing))
    seen = set()
    for row in rows:
        key = (row["expression"], row["targets"])
        if key in seen: errors.append("duplicate expression/target: %s" % (key,))
        seen.add(key)
        if row["owner_plan"] not in owners or row["owner_plan"] not in ALLOWED:
            errors.append("unowned row: " + row["expression"])
        if row["owner_plan"] != owner_for(row["source"]):
            errors.append("owner/source partition mismatch: " + row["expression"])
        if not row["targets"]: errors.append("target missing: " + row["expression"])
    if errors:
        raise SystemExit("\n".join(errors))

def read_inventory(path):
    with path.open(newline="") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))

def semantic_rows(rows, include_disposition=False):
    fields = FIELDS if include_disposition else [field for field in FIELDS if field != "disposition"]
    return sorted(
        tuple(row[field] for field in fields)
        for row in rows
    )

def check_current(inventory, commands):
    actual = rows(commands)
    if semantic_rows(read_inventory(inventory)) != semantic_rows(actual):
        raise SystemExit("inventory differs from current declaration/call discovery")

def check_owner(inventory, commands, owner, sources, required_targets):
    actual = [row for row in rows(commands) if row["source"] in sources]
    owned = [row for row in read_inventory(inventory) if row["owner_plan"] == owner and row["source"] in sources]
    if semantic_rows(owned) != semantic_rows(actual):
        raise SystemExit("owner rows differ from current source discovery")
    target_set = {target for row in owned for target in row["targets"].split(";") if target}
    missing = sorted(required_targets - target_set)
    if missing:
        raise SystemExit("owner rows missing required targets: " + ", ".join(missing))

def check_migrated(inventory, commands, owner):
    check_current(inventory, commands)
    owned = [row for row in read_inventory(inventory) if row["owner_plan"] == owner]
    # Plan 14-07 owns the five account/blockchain fixture targets, not the
    # unrelated historical default rows that remain staged for later partitions.
    if owner == "14-07":
        owned = [row for row in owned if row["source"] in FIXTURE_SOURCES]
    if owner == "14-08":
        owned = [row for row in owned if row["source"] in PLAN_14_08_SOURCES]
    if owner == "14-09":
        owned = [row for row in owned if row["source"] in PLAN_14_09_SOURCES]
    if owner == "14-10":
        owned = [row for row in owned if row["source"] in PLAN_14_10_SOURCES]
    pending = [row["expression"] for row in owned if row["disposition"] != "migrated"]
    if pending:
        raise SystemExit("unmigrated owner rows: " + ", ".join(pending))

def check_all_migrated(inventory, commands, targets_output, sources_output):
    check_current(inventory, commands)
    all_rows = read_inventory(inventory)
    check(all_rows, ALLOWED)
    pending = [row["expression"] for row in all_rows if row["disposition"] != "migrated"]
    if pending:
        raise SystemExit("unmigrated rows: " + ", ".join(pending))
    targets = sorted({target for row in all_rows for target in row["targets"].split(";") if target})
    sources = sorted({row["source"] for row in all_rows})
    if "header-consumer" in targets:
        raise SystemExit("unresolved header-consumer target in final caller union")
    if targets_output:
        targets_output.write_text("\n".join(targets) + "\n")
    if sources_output:
        sources_output.write_text("\n".join(sources) + "\n")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compile-commands", type=pathlib.Path, default=ROOT / "build/OSX/Release/compile_commands.json")
    parser.add_argument("--write-inventory", type=pathlib.Path)
    parser.add_argument("--inventory", type=pathlib.Path)
    parser.add_argument("--check-assigned", action="store_true")
    parser.add_argument("--owners", default="")
    parser.add_argument("--require-exact-owner-per-expression-target", action="store_true")
    parser.add_argument("--check-owner-source-partition", action="store_true")
    parser.add_argument("--check-current", action="store_true")
    parser.add_argument("--check-owner")
    parser.add_argument("--sources", default="")
    parser.add_argument("--required-targets", default="")
    parser.add_argument("--check-migrated", action="store_true")
    parser.add_argument("--owner")
    parser.add_argument("--check-all-migrated", action="store_true")
    parser.add_argument("--targets-output", type=pathlib.Path)
    parser.add_argument("--sources-output", type=pathlib.Path)
    args = parser.parse_args()
    check_receiver_self_test()
    if args.write_inventory:
        generated = rows(args.compile_commands)
        previous_dispositions = {}
        if args.write_inventory.exists():
            previous_dispositions = {
                (row["method"], row["source"], row["targets"]): row["disposition"]
                for row in read_inventory(args.write_inventory)
            }
        for row in generated:
            row["disposition"] = previous_dispositions.get(
                (row["method"], row["source"], row["targets"]), row["disposition"] )
        args.write_inventory.parent.mkdir(parents=True, exist_ok=True)
        with args.write_inventory.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=FIELDS, delimiter="\t", lineterminator="\n")
            writer.writeheader(); writer.writerows(generated)
    if args.check_assigned:
        check(read_inventory(args.inventory), set(filter(None, args.owners.split(","))))
    if args.check_current:
        check_current(args.inventory, args.compile_commands)
    if args.check_owner:
        check_owner(args.inventory,
                    args.compile_commands,
                    args.check_owner,
                    set(filter(None, args.sources.split(","))),
                    set(filter(None, args.required_targets.split(","))))
    if args.check_migrated:
        if not args.owner:
            parser.error("--check-migrated requires --owner")
        check_migrated(args.inventory, args.compile_commands, args.owner)
    if args.check_all_migrated:
        check_all_migrated(args.inventory, args.compile_commands, args.targets_output, args.sources_output)
    print("phase14_node_api_caller_audit=PASS")

if __name__ == "__main__":
    main()
