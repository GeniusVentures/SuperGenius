#!/usr/bin/env python3
"""
find_contract_creation_blocks.py — Discover bridge contract creation blocks
using the GeniusDiamond deployment manifest and public RPC endpoints.

Workflow:
  1. Download deployments.json from GeniusDiamond GitHub releases (latest).
  2. Cross-reference DiamondAddress + chainId with bridge_chains_config.json:
     - Mismatched addresses → warning (config may be stale).
     - Missing GNUSBridge facet → warning (bridge not deployed).
  3. For each chain with creation_block = 0, binary-search eth_getCode
     against the GNUSBridge facet address (preferred) or the Diamond address
     (fallback), using built-in public RPC endpoints.
  4. Write creation_block back to bridge_chains_config.json.

No --rpc flags are required — each chain has a built-in public endpoint.
Use --rpc chain_name=URL to override the default.

When the deployment manifest adds block_number fields, step 3 becomes a
no-op and creation blocks are sourced directly from the manifest.

Usage:
  python3 scripts/find_contract_creation_blocks.py src/account/bridge_chains_config.json
  python3 scripts/find_contract_creation_blocks.py src/account/bridge_chains_config.json --force
  python3 scripts/find_contract_creation_blocks.py src/account/bridge_chains_config.json --no-manifest
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
    "mainnet":      "ethereum-mainnet",
    "sepolia":      "ethereum-sepolia",
    "bsc":          "bnb-smart-chain",
    "bsc_testnet":  "bnb-smart-chain-testnet",
    "polygon":      "polygon-mainnet",
    "polygon_amoy": "polygon-amoy",
    "base":         "base-mainnet",
    "base_sepolia": "base-sepolia",
}

DEPLOYMENTS_URL = (
    "https://github.com/GeniusVentures/GeniusDiamond/releases/latest/"
    "download/deployments.json"
)

# ── Built-in public RPC endpoints (free, no API key needed) ──────────────
# Keyed by numeric chain_id.
PUBLIC_RPC: dict[int, str] = {
    1:       "https://eth.drpc.org",
    56:      "https://bsc.drpc.org",
    97:      "https://bsc-testnet.drpc.org",
    137:     "https://polygon.drpc.org",
    8453:    "https://mainnet.base.org",
    84532:   "https://sepolia.base.org",
    80002:   "https://polygon-amoy.drpc.org",
    11155111: "https://sepolia.drpc.org",
}

# Facet names that carry the bridge-out function (checked in order).
BRIDGE_FACET_NAMES: list[str] = ["GNUSBridge", "PolyGNUSBridge"]


# ── Network helpers ────────────────────────────────────────────────────────

# Many public RPCs reject Python's default urllib User-Agent.
# A browser-like UA avoids 403 responses.
_HTTP_HEADERS: dict[str, str] = {
    "Content-Type": "application/json",
    "User-Agent": "Mozilla/5.0 (compatible; SuperGenius-build-script/1.0)",
}


def _build_req(url: str, data: Optional[bytes] = None) -> request.Request:
    """Create a Request with standard headers."""
    return request.Request(url, data=data, headers=_HTTP_HEADERS)


def fetch_deployments() -> Optional[dict]:
    """Download and parse the GeniusDiamond deployment manifest."""
    try:
        with request.urlopen(_build_req(DEPLOYMENTS_URL), timeout=30) as resp:
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
    try:
        with request.urlopen(_build_req(rpc_url, data=payload), timeout=30) as resp:
            data = json.loads(resp.read())
    except (URLError, OSError, json.JSONDecodeError) as exc:
        print(f"  HTTP/parse error: {exc}", file=sys.stderr)
        return None
    if "error" in data:
        print(f"  RPC error: {data['error']}", file=sys.stderr)
        return None
    return data


def eth_get_transaction_receipt(rpc_url: str, tx_hash: str) -> Optional[int]:
    """
    Get the block number from a transaction receipt by tx hash.
    One RPC call — works on non-archive nodes for historical txs.
    Returns block number or None.
    """
    data = rpc_call(rpc_url, "eth_getTransactionReceipt", [tx_hash])
    if data is None:
        return None
    result = data.get("result")
    if not result or result == "0x":
        print(f"  Transaction receipt not found for {tx_hash}", file=sys.stderr)
        return None
    block_hex = result.get("blockNumber", "0x0")
    try:
        return int(block_hex, 16)
    except (ValueError, TypeError):
        print(f"  Invalid block number in receipt: {block_hex}", file=sys.stderr)
        return None


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

    code = eth_get_code(rpc_url, contract_addr, low)
    if code is None or code == "0x":
        print(f"  Verification failed at block {low}", file=sys.stderr)
        return None

    return low


# ── Manifest cross-reference ────────────────────────────────────────────────

def cross_reference_manifest(config: dict, deployments: dict) -> tuple[int, dict[str, dict]]:
    """
    Cross-reference bridge_chains_config.json against the deployment manifest.
    Returns (warning_count, facet_map: config_chain_name → {address, tx_hash}).
    """
    warnings = 0
    facet_map: dict[str, dict] = {}

    for depl_key, depl_entry in deployments.items():
        if not isinstance(depl_entry, dict):
            continue
        config_key = DEPLOYMENT_TO_CONFIG_CHAIN.get(depl_key)
        if config_key is None:
            continue
        if config_key not in config:
            print(f"  Note: '{depl_key}' maps to '{config_key}' not in config")
            continue

        cfg = config[config_key]
        depl_addr = depl_entry.get("DiamondAddress", "").lower()
        cfg_addr = cfg.get("bridge_contract_address", "").lower()

        if depl_addr != cfg_addr:
            print(f"  WARNING: {config_key}: config has {cfg_addr}, "
                  f"manifest has {depl_addr}", file=sys.stderr)
            warnings += 1

        facets = depl_entry.get("FacetDeployedInfo", {})
        bridge_facet_info = None
        for name in BRIDGE_FACET_NAMES:
            if name in facets:
                bridge_facet_info = {
                    "address": facets[name]["address"],
                    "tx_hash": facets[name].get("tx_hash", ""),
                }
                break

        if bridge_facet_info:
            facet_map[config_key] = bridge_facet_info
        else:
            print(f"  WARNING: {config_key}: no {', '.join(BRIDGE_FACET_NAMES)} facet "
                  "— falling back to Diamond address for creation block",
                  file=sys.stderr)
            warnings += 1

    return warnings, facet_map


# ── Main ────────────────────────────────────────────────────────────────────

def main() -> None:
    parser = ArgumentParser(
        description="Discover bridge contract creation blocks using the "
                    "GeniusDiamond manifest + public RPC endpoints.")
    parser.add_argument("config", help="Path to bridge_chains_config.json")
    parser.add_argument(
        "--rpc", action="append", default=[],
        help="Override public RPC endpoint for a chain (chain_name=URL), repeatable")
    parser.add_argument(
        "--no-manifest", action="store_true",
        help="Skip downloading deployments.json (offline/CI)")
    parser.add_argument(
        "--force", action="store_true",
        help="Rediscover creation_block even for chains that already have one")
    args = parser.parse_args()

    # ── Parse RPC overrides ───────────────────────────────────────────────
    rpc_overrides: dict[str, str] = {}
    for entry in args.rpc:
        if "=" not in entry:
            print(f"Invalid --rpc format: {entry} (expected chain_name=url)",
                  file=sys.stderr)
            sys.exit(1)
        name, url = entry.split("=", 1)
        rpc_overrides[name] = url

    # ── Read config ───────────────────────────────────────────────────────
    with open(args.config, "r") as f:
        config = json.load(f)

    # ── Fetch + cross-reference deployment manifest ───────────────────────
    facet_map: dict[str, dict] = {}
    if not args.no_manifest:
        print(f"Fetching deployments.json ...")
        deployments = fetch_deployments()
        if deployments is not None:
            print(f"  {len(deployments)} chain(s) in manifest")
            _, facet_map = cross_reference_manifest(config, deployments)
            chains_with_facet = len(facet_map)
            print(f"  {chains_with_facet} chain(s) have a bridge facet address")
        else:
            print("  Skipping manifest cross-reference (fetch failed)")
    else:
        print("Skipping manifest fetch (--no-manifest)")

    # ── Discover creation blocks ──────────────────────────────────────────
    print()
    updated = 0
    errors = 0

    for chain_name, chain_data in config.items():
        if chain_name.startswith("_"):
            continue

        current_block = chain_data.get("creation_block", 0)
        if current_block > 0 and not args.force:
            print(f"Skipping {chain_name}: creation_block already {current_block}")
            continue

        chain_id = chain_data.get("chain_id", 0)
        diamond_addr = chain_data.get("bridge_contract_address", "")
        if not diamond_addr:
            print(f"Skipping {chain_name}: no bridge_contract_address")
            continue

        # ── Resolve RPC URL: override > built-in ──────────────────────────
        rpc_url = rpc_overrides.get(chain_name)
        if rpc_url is None:
            rpc_url = PUBLIC_RPC.get(chain_id)
        if rpc_url is None:
            print(f"Skipping {chain_name}: no public RPC endpoint for chain_id {chain_id}")
            continue

        facet_info = facet_map.get(chain_name)

        print(f"{chain_name} (chain_id={chain_id}):")
        print(f"  rpc:    {rpc_url}")

        creation = None

        # ── Try tx receipt first (one call, works on non-archive nodes) ───
        if facet_info and facet_info.get("tx_hash"):
            tx_hash = facet_info["tx_hash"]
            facet_addr = facet_info["address"]
            print(f"  target: {facet_addr} (GNUSBridge facet)")
            print(f"  try:    eth_getTransactionReceipt({tx_hash}) ...")
            creation = eth_get_transaction_receipt(rpc_url, tx_hash)
            if creation is not None:
                print(f"  -> deployed at block {creation} (via tx receipt)")
            else:
                print(f"  tx receipt not found, falling back to eth_getCode binary search")

        if creation is None:
            # ── Resolve target: facet address or diamond fallback ─────────
            if facet_info:
                target_addr = facet_info["address"]
                target_label = "GNUSBridge facet"
            else:
                target_addr = diamond_addr
                target_label = "Diamond (no bridge facet in manifest)"

            print(f"  target: {target_addr} ({target_label})")

            latest = eth_block_number(rpc_url)
            if latest is None:
                print(f"  FAILED to get current block number", file=sys.stderr)
                errors += 1
                continue

            creation = binary_search_creation(rpc_url, target_addr, latest)

        if creation is not None:
            chain_data["creation_block"] = creation
            updated += 1
        else:
            print(f"  -> FAILED to find creation block", file=sys.stderr)
            errors += 1

    # ── Write back ────────────────────────────────────────────────────────
    if updated > 0:
        with open(args.config, "w") as f:
            json.dump(config, f, indent=4)
            f.write("\n")
        print(f"\nUpdated {updated} chain(s) in {args.config}")

    # Post-run summary: flag chains where creation_block is still 0, since the
    # BridgeCatchupWatcher falls back to config_.start_block (possibly genesis)
    # for those, which can cause a multi-million-block scan (WR-08).
    zero_block_chains = []
    for chain_name, chain_data in config.items():
        if chain_name.startswith("_"):
            continue
        if not isinstance(chain_data, dict):
            continue
        if chain_data.get("creation_block", 0) == 0:
            zero_block_chains.append(chain_name)
    if zero_block_chains:
        print("\nSummary:", file=sys.stderr)
        for name in zero_block_chains:
            print(f"  WARNING: {name} has creation_block = 0 — "
                  "watcher will scan from config start_block (possibly genesis)",
                  file=sys.stderr)

    if errors > 0:
        print(f"\n{errors} error(s) encountered", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
