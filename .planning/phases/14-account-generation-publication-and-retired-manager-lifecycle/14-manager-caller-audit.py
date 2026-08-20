#!/usr/bin/env python3
"""Deterministic, comment/string-aware TransactionManager caller manifest."""
import argparse, json, re, sys
from pathlib import Path

METHODS = ("TransferFunds", "MintFunds", "MigrationFunds", "HoldEscrow", "PayEscrow", "AsyncPayEscrow", "SubmitTransaction", "EnqueueTransaction", "GetOutTransactions", "CountTransactions", "GetState", "GetTransactionStatusByTxId", "WaitForTransactionIncoming", "WaitForTransactionOutgoing", "RegisterStateChangeCallback", "RegisterTopicNames", "StartCore", "Start", "Stop")
FIXTURE_MANAGER_ACCESS_METHODS = ("GetTransactionManager", "GetPublicChainInputValidator")
ROOTS = ("src", "test", "apps", "example")
ALLOWED = {"src/account/GeniusNode.cpp", "src/account/BridgeRelayer.cpp", "src/migration/Migration3_6_0To3_7_0.cpp", "test/src/account/burnconfig_policy_e2e_test.cpp", "test/src/account/transaction_manager_pending_lifecycle_test.cpp", "test/src/blockchain/consensus_subject_test.cpp", "test/src/multiaccount/multi_account_sync.cpp", "test/src/multiaccount/policy_lifetime_multi_account_test.cpp", "test/src/bridge_race/bridge_race_fault_rpc_test.cpp"}
PLAN4 = {"test/src/account/burnconfig_policy_e2e_test.cpp", "test/src/account/transaction_manager_pending_lifecycle_test.cpp", "test/src/blockchain/consensus_subject_test.cpp", "test/src/bridge_race/bridge_race_fault_rpc_test.cpp"}
PLAN5 = {"test/src/multiaccount/multi_account_sync.cpp", "test/src/multiaccount/policy_lifetime_multi_account_test.cpp"}

def scrub(text):
    return re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', lambda m: "\n" * m.group(0).count("\n") + " " * (len(m.group(0)) - m.group(0).count("\n")), text)
def owner(path):
    return "14-04" if path in PLAN4 else "14-05" if path in PLAN5 else "14-03"
def targets(path, commands):
    hit=[]
    for entry in commands:
        if Path(entry.get("file", "")).as_posix().endswith(path):
            hit += re.findall(r"CMakeFiles/([^/]+)\.dir", entry.get("command", ""))
    if path in {"src/account/GeniusNode.cpp", "src/account/BridgeRelayer.cpp"}: hit += ["genius_node", "genius_node_test"]
    if path == "src/migration/Migration3_6_0To3_7_0.cpp": hit += ["migration"]
    return ",".join(sorted(set(hit))) or "unresolved"
def rows(commands, disposition="pending"):
    out=[]
    for root in ROOTS:
        if not Path(root).exists(): continue
        for file in Path(root).rglob("*.cpp"):
            path=file.as_posix(); text=scrub(file.read_text(errors="ignore"))
            if path not in ALLOWED: continue
            methods = METHODS
            if path == "test/src/bridge_race/bridge_race_fault_rpc_test.cpp":
                methods += FIXTURE_MANAGER_ACCESS_METHODS
            for method in methods:
                for match in re.finditer(r"(?:->|\.)\s*(%s)\s*\(" % method, text):
                    line=text.count("\n", 0, match.start()) + 1
                    out.append((method, f"{method}@{line}", path, targets(path, commands), owner(path), disposition))
    return sorted(set(out), key=lambda r:(r[2], r[1]))
def load_commands(path): return json.loads(Path(path).read_text())
def identities(data):
    return [row[:5] for row in data]

def validate(data):
    assert all(len(row)==6 and row[3] != "unresolved" and row[4] in {"14-03","14-04","14-05"} for row in data)
    assert len(identities(data)) == len(set(identities(data)))

def main():
    p=argparse.ArgumentParser(); p.add_argument("--write-inventory"); p.add_argument("--inventory"); p.add_argument("--compile-commands", required=True); p.add_argument("--check-assigned", action="store_true"); p.add_argument("--owners"); p.add_argument("--check-current", action="store_true"); p.add_argument("--check-owner"); p.add_argument("--sources"); p.add_argument("--required-targets"); p.add_argument("--check-migrated", action="store_true"); p.add_argument("--owner"); p.add_argument("--refresh-owner"); p.add_argument("--mark-migrated"); args=p.parse_args(); commands=load_commands(args.compile_commands)
    if args.write_inventory:
        data=rows(commands); Path(args.write_inventory).write_text("method\texpression\tsource\ttargets\towner_plan\tdisposition\n"+"".join("\t".join(r)+"\n" for r in data)); return
    lines=Path(args.inventory).read_text().splitlines(); assert lines[0] == "method\texpression\tsource\ttargets\towner_plan\tdisposition"
    data=[tuple(x.split("\t")) for x in lines[1:]]; validate(data)
    if args.refresh_owner or args.mark_migrated:
        refreshed_owner = args.mark_migrated or args.refresh_owner
        current = rows(commands, "migrated" if args.mark_migrated else "pending")
        owner_rows = [row for row in current if row[4] == refreshed_owner]
        assert owner_rows
        preserved = [row for row in data if row[4] != refreshed_owner]
        data = sorted(preserved + owner_rows, key=lambda row:(row[2], row[1]))
        validate(data)
        Path(args.inventory).write_text("method\texpression\tsource\ttargets\towner_plan\tdisposition\n"+"".join("\t".join(row)+"\n" for row in data))
    if args.check_current:
        current = [row for row in rows(commands) if row[4] == "14-03"]
        recorded = [row for row in data if row[4] == "14-03"]
        assert identities(recorded) == identities(current)
    if args.check_assigned: assert all(x[4] in set(args.owners.split(",")) for x in data)
    if args.check_owner:
        wanted_sources = set(args.sources.split(",")) if args.sources else None
        owner_rows = [row for row in data if row[4] == args.check_owner]
        assert owner_rows
        current_owner_rows = [row for row in rows(commands) if row[4] == args.check_owner]
        assert identities(owner_rows) == identities(current_owner_rows)
        if wanted_sources:
            assert {row[2] for row in owner_rows} <= wanted_sources
            assert all(targets(source, commands) != "unresolved" for source in wanted_sources)
        if args.required_targets:
            required = set(args.required_targets.split(","))
            actual = {target for source in wanted_sources for target in targets(source, commands).split(",")}
            assert required <= actual
    if args.check_migrated:
        assert args.owner
        owner_rows = [row for row in data if row[4] == args.owner]
        assert owner_rows and all(row[5] == "migrated" for row in owner_rows)
if __name__ == "__main__": main()
