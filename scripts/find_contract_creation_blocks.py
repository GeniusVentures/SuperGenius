#!/usr/bin/env python3
"""
find_contract_creation_blocks.py — Verify bridge contract addresses against the
GeniusDiamond deployment manifest and discover creation blocks via eth_getCode
binary search.

Workflow:
  1. Download deployments.json from GeniusDiamond GitHub releases (latest).
  2. Cross-reference DiamondAddress + chainId with bridge_chains_config.json.
     - Mismatched addresses → warning (config may be stale).
     - Missing GNUSBridge facet → warning (bridge not deployed on that chain).
  3. For each chain with creation_block = 0, binary-search eth_getCode
     using the RPC URL from --rpc to find the exact deployment block.
  4. Write creation_block back to bridge_chains_config.json.

When the deployment manifest adds block_number fields, step 3 becomes a
no-op and creation blocks are sourced directly from the manifest.

Usage:
  # All chains with RPC URLs:
  python3 scripts/find_contract_creation_blocks.py src/account/bridge_chains_config.json \\
      --rpc "ethereum-sepolia=$RPC_SEPOLIA" --rpc "base-sepolia=$RPC_BASE_SEPOLIA" ...

  # Skip manifest fetch (offline / CI):
  python3 scripts/find_contract_creation_blocks.py src/account/bridge_chains_config.json \\
      --rpc "ethereum-sepolia=$RPC_SEPOLIA" --no-manifest

  # Force rediscovery:
  python3 scripts/find_contract_creation_blocks.py src/account/bridge_chains_config.json \\
      --rpc "ethereum-sepolia=$RPC_SEPOLIA" --force
"""
import json
import sys
import time
from argparse import ArgumentParser
from typing import Optional
from urllib import request
from urllib.error import URLError

# ── Chain name mapping: deployments.json key → bridge_chains_config.json key ──
DEPLOYMENT_TO_CONFIG_CHAIN: dict[str, str] = {
    "mainnet":        "ethereum-mainnet",
    "sepolia":        "ethereum-sepolia",
    "bsc":            "bnb-smart-chain",
    "bsc_testnet":    "bnb-smart-chain-testnet",
    "polygon":        "polygon-mainnet",
    "polygon_amoy":   "polygon-amoy",
    "base":           "base-mainnet",
    "base_sepolia":   "base-sepolia",
}

DEPLOYMENTS_URL = (
    "https://github.com/GeniusVentures/GeniusDiamond/releases/latest/"
    "download/deployments.json"
)


def fetch_deployments() -> Optional[dict]:
    """Download and parse the GeniusDiamond deployment manifest."""
    try:
        req = request.Request(DEPLOYMENTS_URL)
        with request.urlopen(req, timeout=30) as resp:
            return json.loads(resp.read())
    except (URLError, OSError, json.JSONDecodeError) as exc:
        print(f"Failed to fetch deployments.json: {exc}", file=sys.stderr)
        return None


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
            print(f"  RPC unavailable at block {mid} (iteration {iterations}) — "
                  "historical state may be pruned", file=sys.stderr)
            return None
        if code == "0x":
            low = mid + 1
        else:
            high = mid
        if iterations % 5 == 0:
            time.sleep(0.2)

    # Verification at the determined block
    code = eth_get_code(rpc_url, contract_addr, low)
    if code is None or code == "0x":
        print(f"  Verification failed at block {low}", file=sys.stderr)
        return None

    return low


def cross_reference_manifest(config: dict, deployments: dict) -> int:
    """
    Cross-reference bridge_chains_config.json entries against the deployment
    manifest.  Returns the number of warnings emitted.
    """
    warnings = 0

    # Build lookup: config_chain_name → deployment_entry
    # Start from deployments keys, map through DEPLOYMENT_TO_CONFIG_CHAIN
    for depl_key, depl_entry in deployments.items():
        if not isinstance(depl_entry, dict):
            continue
        config_key = DEPLOYMENT_TO_CONFIG_CHAIN.get(depl_key)
        if config_key is None:
            continue  # e.g. "mumbai" — not in our config
        if config_key not in config:
            print(f"  Note: deployment chain '{depl_key}' maps to '{config_key}' "
                  "which is not in bridge_chains_config.json")
            continue

        cfg = config[config_key]
        depl_addr = depl_entry.get("DiamondAddress", "").lower()
        cfg_addr = cfg.get("bridge_contract_address", "").lower()

        if depl_addr != cfg_addr:
            print(f"  WARNING: {config_key}: config address {cfg_addr} differs "
                  f"from deployed DiamondAddress {depl_addr}", file=sys.stderr)
            warnings += 1

        # Check for GNUSBridge facet
        facets = depl_entry.get("FacetDeployedInfo", {})
        has_bridge = "GNUSBridge" in facets or "PolyGNUSBridge" in facets
        if not has_bridge:
            print(f"  WARNING: {config_key}: no GNUSBridge/PolyGNUSBridge facet "
                  "in deployment manifest — bridge may not be active",
                  file=sys.stderr)
            warnings += 1

        # Check for block_number in any facet (future — skip binary search)
        if any("block_number" in f for f in facets.values()):
            print(f"  {config_key}: manifest includes block_number — "
                  "binary search not needed for this chain")

    return warnings


def main() -> None:
    parser = ArgumentParser(
        description="Verify bridge contracts against GeniusDiamond manifest "
                    "and discover creation blocks via eth_getCode binary search.")
    parser.add_argument("config", help="Path to bridge_chains_config.json")
    parser.add_argument(
        "--rpc", action="append", default=[],
        help="RPC URL for a chain (format: chain_name=URL), repeatable")
    parser.add_argument(
        "--no-manifest", action="store_true",
        help="Skip downloading deployments.json (offline/CI mode)")
    parser.add_argument(
        "--force", action="store_true",
        help="Rediscover creation_block even for chains that already have one")
    args = parser.parse_args()

    # ── Parse RPC mapping ──────────────────────────────────────────────────
    rpc_map: dict[str, str] = {}
    for entry in args.rpc:
        if "=" not in entry:
            print(f"Invalid --rpc format: {entry} (expected chain_name=url)",
                  file=sys.stderr)
            sys.exit(1)
        name, url = entry.split("=", 1)
        rpc_map[name] = url

    # ── Read config ────────────────────────────────────────────────────────
    with open(args.config, "r") as f:
        config = json.load(f)

    # ── Fetch + cross-reference deployment manifest ────────────────────────
    if not args.no_manifest:
        print(f"Fetching deployments.json from {DEPLOYMENTS_URL} ...")
        deployments = fetch_deployments()
        if deployments is not None:
            print(f"  Found {len(deployments)} chain(s) in manifest")
            cross_reference_manifest(config, deployments)
        else:
            print("  Skipping manifest cross-reference (fetch failed)")
    else:
        print("Skipping manifest fetch (--no-manifest)")

    if not rpc_map:
        print("\nNo --rpc entries provided.  Nothing to resolve.\n"
              "  Example: --rpc \"ethereum-sepolia=$RPC_SEPOLIA\"",
              file=sys.stderr)
        sys.exit(0)

    # ── Discover creation blocks ───────────────────────────────────────────
    print()
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

    # ── Write back ─────────────────────────────────────────────────────────
    if updated > 0:
        with open(args.config, "w") as f:
            json.dump(config, f, indent=4)
            f.write("\n")
        print(f"\nUpdated {updated} chain(s) in {args.config}")

    if errors > 0:
        print(f"{errors} error(s) encountered", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
