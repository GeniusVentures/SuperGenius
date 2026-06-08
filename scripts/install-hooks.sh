#!/bin/bash
# ==============================================================================
# Install GNUS-NEO-SWARM pre-commit hook
#
# Creates a symlink from .git/hooks/pre-commit → scripts/pre-commit-lint.sh
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

HOOK_SRC="${SCRIPT_DIR}/pre-commit-lint.sh"
HOOK_DST="${REPO_ROOT}/.git/hooks/pre-commit"

if [ ! -f "${HOOK_SRC}" ]; then
    echo "ERROR: ${HOOK_SRC} not found."
    exit 1
fi

# Make the hook script executable
chmod +x "${HOOK_SRC}"

# Remove existing hook if present
if [ -e "${HOOK_DST}" ] || [ -L "${HOOK_DST}" ]; then
    rm -f "${HOOK_DST}"
fi

ln -sf "${HOOK_SRC}" "${HOOK_DST}"

echo "✓  Pre-commit hook installed: .git/hooks/pre-commit → scripts/pre-commit-lint.sh"
