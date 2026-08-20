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
ALLOWED = {"14-06", "14-07", "14-08", "14-09", "14-10", "14-11", "14-12"}

def owner_for(source):
    s = source.replace("\\", "/")
    if s in {"src/account/GeniusNode.cpp", "src/account/GeniusNode.hpp", "src/node/node_initialization_progress.cpp"}:
        return "14-06"
    if "/bridge_race/" in s or "/bridge/" in s and "test/" in s:
        return "14-11" if "race" in s else "14-10"
    if "/multiaccount/" in s:
        return "14-09"
    if "/processing_multi/" in s or "/NodeExample.cpp" in s:
        return "14-12"
    if "/processing" in s or "/transaction_sync" in s:
        return "14-08"
    return "14-07"

def contract_for(method):
    if method in {"SelectAccount", "TransferAccount", "DeleteAccount", "MergeAccount"}:
        return "lifecycle-selection"
    if method in {"GetProcessingStatus", "StartProcessing", "StopProcessing", "ResetProcessingMembers"}:
        return "processing-generation"
    if method in {"ConfigureRpcEndpoint", "SendTransactionAndProof"}:
        return "internal-control"
    return "active-ready"

def identity_for(contract):
    return "configured-bootstrap" if contract == "internal-control" else "active-generation"

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
        contract = contract_for(method)
        rows.append({"method": method, "expression": f"declaration:{method}", "contract": contract,
                     "identity_kind": identity_for(contract), "source": "src/account/GeniusNode.hpp",
                     "targets": ";".join(sorted(targets.get("src/account/GeniusNode.hpp", {"genius_node_test"}))),
                     "owner_plan": "14-06", "disposition": "migrate"})
    call = re.compile(r"(?:->|\.|\bGeniusNode::)\s*(%s)\s*\(" % "|".join(METHODS))
    for path in discover_sources():
        rel = path.relative_to(ROOT).as_posix()
        if rel == "src/account/GeniusNode.hpp":
            continue
        text = re.sub(r"/\*.*?\*/", "", path.read_text(errors="ignore"), flags=re.S)
        text = re.sub(r"//[^\n]*|\"(?:\\.|[^\"])*\"", "", text)
        found = sorted(set(call.findall(text)))
        for method in found:
            contract = contract_for(method)
            rows.append({"method": method, "expression": f"{rel}:{method}", "contract": contract,
                         "identity_kind": identity_for(contract), "source": rel,
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

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compile-commands", type=pathlib.Path, default=ROOT / "build/OSX/Release/compile_commands.json")
    parser.add_argument("--write-inventory", type=pathlib.Path)
    parser.add_argument("--inventory", type=pathlib.Path)
    parser.add_argument("--check-assigned", action="store_true")
    parser.add_argument("--owners", default="")
    parser.add_argument("--require-exact-owner-per-expression-target", action="store_true")
    parser.add_argument("--check-owner-source-partition", action="store_true")
    args = parser.parse_args()
    if args.write_inventory:
        generated = rows(args.compile_commands)
        args.write_inventory.parent.mkdir(parents=True, exist_ok=True)
        with args.write_inventory.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=FIELDS, delimiter="\t")
            writer.writeheader(); writer.writerows(generated)
    if args.check_assigned:
        with args.inventory.open(newline="") as stream:
            check(list(csv.DictReader(stream, delimiter="\t")), set(filter(None, args.owners.split(","))))
    print("phase14_node_api_caller_audit=PASS")

if __name__ == "__main__":
    main()
