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

def rows(commands):
    targets = targets_by_source(commands)
    rows = []
    for method in METHODS:
        contract = contract_for(method, "src/account/GeniusNode.hpp")
        rows.append({"method": method, "expression": f"declaration:{method}", "contract": contract,
                     "identity_kind": identity_for(contract), "source": "src/account/GeniusNode.hpp",
                     "targets": ";".join(sorted(targets.get("src/account/GeniusNode.hpp", {"genius_node_test"}))),
                     "owner_plan": "14-06", "disposition": "migrate"})
    call = re.compile(r"(?:->|\.|\bGeniusNode::)\s*(%s)\s*\(" % "|".join(CALL_METHODS))
    fixture_call = re.compile(
        r"(?:\bnode_?[A-Za-z0-9_]*|fixture\.node)\s*(?:->|\.)\s*(%s)\s*\(" % "|".join(CALL_METHODS))
    for path in discover_sources():
        rel = path.relative_to(ROOT).as_posix()
        if rel == "src/account/GeniusNode.hpp":
            continue
        text = re.sub(r"/\*.*?\*/", "", path.read_text(errors="ignore"), flags=re.S)
        text = re.sub(r"//[^\n]*|\"(?:\\.|[^\"])*\"", "", text)
        matcher = fixture_call if rel in FIXTURE_SOURCES else call
        found = sorted(set(matcher.findall(text)))
        for called_method in found:
            method = CALL_METHODS[called_method]
            # The shared bridge-race fixture owns a separately constructed account
            # identity during pre-ready bootstrap; all other fixture node calls use
            # the checked active-generation surface.
            contract = ("configured-bootstrap" if rel == BRIDGE_RACE_FIXTURE and called_method == "GetAddress"
                        else contract_for(method, rel))
            row_targets = BRIDGE_RACE_TARGETS if rel == BRIDGE_RACE_FIXTURE else targets.get(rel, {"header-consumer"})
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
    args = parser.parse_args()
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
            writer = csv.DictWriter(stream, fieldnames=FIELDS, delimiter="\t")
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
    print("phase14_node_api_caller_audit=PASS")

if __name__ == "__main__":
    main()
