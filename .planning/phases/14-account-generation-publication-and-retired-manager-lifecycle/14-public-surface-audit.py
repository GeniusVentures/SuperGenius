#!/usr/bin/env python3
"""Comment/string-aware TransactionManager public declaration inventory."""
import argparse
import re
import sys
from pathlib import Path


def strip_comments_and_strings(text):
    out, i, state = [], 0, "code"
    while i < len(text):
        pair = text[i : i + 2]
        char = text[i]
        if state == "code" and pair == "//":
            state = "line"; out.extend("  "); i += 2; continue
        if state == "code" and pair == "/*":
            state = "block"; out.extend("  "); i += 2; continue
        if state == "line":
            out.append("\n" if char == "\n" else " ")
            if char == "\n": state = "code"
            i += 1; continue
        if state == "block":
            if pair == "*/": state = "code"; out.extend("  "); i += 2
            else: out.append("\n" if char == "\n" else " "); i += 1
            continue
        if state == "code" and char in "\"'":
            quote = char; state = "string"; out.append(" "); i += 1
            while i < len(text):
                if text[i] == "\\": out.extend("  "); i += 2; continue
                if text[i] == quote: out.append(" "); i += 1; break
                out.append("\n" if text[i] == "\n" else " "); i += 1
            state = "code"; continue
        out.append(char); i += 1
    return "".join(out)


def declarations(path):
    text = strip_comments_and_strings(Path(path).read_text())
    start = text.index("    public:", text.index("class TransactionManager"))
    end = text.index("    protected:", start)
    section = text[start:end]
    result = []
    for match in re.finditer(r"(?:^|\n)\s*(?!using\b|enum\b|struct\b|class\b|static constexpr\b)([^;{}]+?)\s*(?:;|\{)", section, re.S):
        decl = re.sub(r"\s+", " ", match.group(1)).strip()
        name = re.search(r"(?:~?)([A-Za-z_]\w*)\s*\(", decl)
        if not name or "operator=" in decl or name.group(1) in {"if", "switch", "return"}: continue
        method = name.group(1)
        if method in {"TransactionManager", "AdmittedOperation", "RetirementSnapshot"}: continue
        if method in {"TransferFunds", "MintFunds", "MigrationFunds", "HoldEscrow", "PayEscrow", "AsyncPayEscrow", "SubmitTransaction"}:
            category = "external mutation"
        elif decl.startswith("static "):
            category = "static utility"
        elif method in {"GetRetirementSnapshot"}:
            category = "retired-safe snapshot read"
        elif method.startswith(("Get", "Count", "Wait")):
            category = "active-only read"
        else:
            category = "internal continuation/control"
        result.append((method, decl, category))
    return result


def write_inventory(header, output):
    rows = declarations(header)
    if len({signature for _, signature, _ in rows}) != len(rows):
        raise SystemExit("duplicate public declaration")
    Path(output).write_text("method\tsignature\tclassification\n" + "".join(
        f"{method}\t{signature}\t{category}\n" for method, signature, category in rows))


def check(header, inventory):
    expected = declarations(header)
    lines = Path(inventory).read_text().splitlines()
    if not lines or lines[0] != "method\tsignature\tclassification":
        raise SystemExit("invalid public surface header")
    actual = [tuple(line.split("\t")) for line in lines[1:]]
    if any(len(row) != 3 or not row[2] for row in actual):
        raise SystemExit("unclassified public declaration")
    if actual != expected:
        raise SystemExit("public declaration inventory does not exactly match header")


parser = argparse.ArgumentParser()
parser.add_argument("--check-core", action="store_true")
parser.add_argument("header")
parser.add_argument("inventory")
args = parser.parse_args()
if args.check_core:
    check(args.header, args.inventory)
else:
    write_inventory(args.header, args.inventory)
