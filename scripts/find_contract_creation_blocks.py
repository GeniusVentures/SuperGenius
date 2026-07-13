#!/usr/bin/env python3
"""
find_contract_creation_blocks.py — Binary-search eth_getCode to find bridge
contract deployment blocks, updating bridge_chains_config.json in-place.

Usage (one --rpc per chain):
  python3 scripts/find_contract_creation_blocks.py src/account/bridge_chains_config.json \
      --rpc "ethereum-sepolia=${RPC_SEPOLIA}"

The script only queries chains that have creation_block = 0 (unknown).
Chains with a non-zero creation_block are skipped — rerun after deleting
the field to force rediscovery.
"""
import json
import sys
import time
from urllib import request
from urllib.error import URLError
from argparse import ArgumentParser
from typing import Optional


def rpc_call(rpc_url: str, method: str, params: list) -> Optional[dict]:
    """Send a JSON-RPC call. Returns the parsed result dict or None on error."""
    payload = json.dumps({
        "jsonrpc": "2.0",
        "method": method,
        "params": params,
        "id": 1,
    }).encode()
    req = request.Request(rpc_url, data=payload,
                          headers={"Content-Type": "application/json"})
    try:
        with request.urlopen(req, timeout=30) as resp:
            data = json.loads(resp.read())
    except (URLError, OSError, json.JSONDecodeError) as exc:
        print(f"  HTTP/parse error: {exc}", file=sys.stderr)
        return None
    if "error" in data:
        print(f"  RPC error: {data['error']}", file=sys.stderr)
        return None
    return data


def eth_get_code(rpc_url: str, contract_addr: str, block: int) -> Optional[str]:
    """Return the code hex at a given block, or None on error."""
    data = rpc_call(rpc_url, "eth_getCode", [contract_addr, hex(block)])
    if data is None:
        return None
    return data.get("result", "0x")


def eth_block_number(rpc_url: str) -> Optional[int]:
    """Return the current block number, or None on error."""
    data = rpc_call(rpc_url, "eth_blockNumber", [])
    if data is None:
        return None
    result = data.get("result", "0x0")
    try:
        return int(result, 16)
    except (ValueError, TypeError):
        print(f"  Invalid block number response: {result}", file=sys.stderr)
        return None


def binary_search_creation(rpc_url: str, contract_addr: str,
                           latest: int) -> Optional[int]:
    """
    Binary search for the exact block at which the contract was deployed.
    Returns the creation block number, or None if it cannot be determined.
    """
    # Confirm the contract exists at latest
    code = eth_get_code(rpc_url, contract_addr, latest)
    if code is None:
        return None
    if code == "0x":
        print(f"  Contract {contract_addr} has no code at latest block {latest}",
              file=sys.stderr)
        return None

    low = 0
    high = latest
    iterations = 0

    while low < high:
        mid = (low + high) // 2
        code = eth_get_code(rpc_url, contract_addr, mid)
        iterations += 1
        if code is None:
            # Historical state unavailable — search range may include
            # blocks before the RPC provider's archive boundary.
            print(f"  RPC unavailable at block {mid} (iteration {iterations}) — "
                  "historical state may be pruned", file=sys.stderr)
            return None
        if code == "0x":
            low = mid + 1
        else:
            high = mid
        # Rate-limit: small delay between calls
        if iterations % 5 == 0:
            time.sleep(0.2)

    # Verification at the determined block
    code = eth_get_code(rpc_url, contract_addr, low)
    if code is None or code == "0x":
        print(f"  Verification failed at block {low}", file=sys.stderr)
        return None

    return low


def main() -> None:
    parser = ArgumentParser(
        description="Find bridge contract creation blocks via eth_getCode binary search")
    parser.add_argument("config", help="Path to bridge_chains_config.json")
    parser.add_argument(
        "--rpc", action="append", default=[],
        help="RPC URL for a chain (format: chain_name=URL), repeatable")
    parser.add_argument(
        "--force", action="store_true",
        help="Rediscover creation_block even for chains that already have one")
    args = parser.parse_args()

    # Parse RPC mapping from --rpc arguments
    rpc_map: dict[str, str] = {}
    for entry in args.rpc:
        if "=" not in entry:
            print(f"Invalid --rpc format: {entry} (expected chain_name=url)",
                  file=sys.stderr)
            sys.exit(1)
        name, url = entry.split("=", 1)
        rpc_map[name] = url

    if not rpc_map:
        print("No --rpc entries provided. Nothing to do.", file=sys.stderr)
        print("Example: python3 scripts/find_contract_creation_blocks.py "
              'src/account/bridge_chains_config.json --rpc "ethereum-sepolia=$RPC_SEPOLIA"',
              file=sys.stderr)
        sys.exit(0)

    # Read config
    with open(args.config, "r") as f:
        config = json.load(f)

    updated = 0
    errors = 0

    for chain_name, chain_data in config.items():
        if chain_name.startswith("_"):
            continue
        if chain_name not in rpc_map:
            continue

        rpc_url = rpc_map[chain_name]
        contract_addr = chain_data.get("bridge_contract_address", "")
        if not contract_addr:
            print(f"Skipping {chain_name}: no bridge_contract_address")
            continue

        current_block = chain_data.get("creation_block", 0)
        if current_block > 0 and not args.force:
            print(f"Skipping {chain_name}: creation_block already {current_block}")
            continue

        print(f"Finding creation block for {chain_name} ({contract_addr})...")

        latest = eth_block_number(rpc_url)
        if latest is None:
            print(f"  FAILED to get current block number", file=sys.stderr)
            errors += 1
            continue

        creation = binary_search_creation(rpc_url, contract_addr, latest)
        if creation is not None:
            chain_data["creation_block"] = creation
            updated += 1
            print(f"  -> deployed at block {creation} "
                  f"({latest - creation} blocks ago, {latest} current)")
        else:
            print(f"  -> FAILED to find creation block", file=sys.stderr)
            errors += 1

    if updated > 0:
        with open(args.config, "w") as f:
            json.dump(config, f, indent=4)
            f.write("\n")
        print(f"\nUpdated {updated} chain(s) in {args.config}")
    else:
        print("\nNo updates made.")

    if errors > 0:
        print(f"{errors} error(s) encountered", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
