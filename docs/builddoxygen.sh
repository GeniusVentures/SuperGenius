#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DOXYFILE="$ROOT_DIR/docs/Doxyfile"

mkdir -p doxygen
cd ..

if [[ ! -f "$DOXYFILE" ]]; then
  echo "Doxyfile not found at: $DOXYFILE" >&2
  exit 1
fi

doxygen "$DOXYFILE"

cd docs
