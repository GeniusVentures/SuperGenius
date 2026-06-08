#!/bin/bash
# ==============================================================================
# Pre-commit lint hook — GNUS-NEO-SWARM
#
# Runs clang-tidy and clang-format on staged C++ files.
# - clang-tidy + clang-format must be on PATH (hard fail if missing).
# - Warnings are emitted but the commit proceeds (lax mode).
# - Full enforcement comes later in the refactor phases.
#
# Install:  bash scripts/install-hooks.sh
# ==============================================================================

set -euo pipefail

CLANG_TIDY=$(which clang-tidy 2>/dev/null || true)
CLANG_FORMAT=$(which clang-format 2>/dev/null || true)

# ── Hard fail if tools are missing ──
if [ -z "${CLANG_TIDY}" ]; then
    echo "ERROR: clang-tidy not found on PATH. Install it (CLion bundles it) or add to PATH."
    echo "       e.g.: export PATH=\"/Applications/CLion.app/Contents/bin/clang/mac/aarch64/bin:\$PATH\""
    exit 1
fi
if [ -z "${CLANG_FORMAT}" ]; then
    echo "ERROR: clang-format not found on PATH. Install it (CLion bundles it) or add to PATH."
    exit 1
fi

STAGED=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\.(hpp|cpp|h|c)$' || true)

if [ -z "${STAGED}" ]; then
    exit 0
fi

HAS_WARNINGS=0

for FILE in ${STAGED}; do
    if [ ! -f "${FILE}" ]; then
        continue
    fi

    echo ""
    echo "── clang-tidy: ${FILE} ──"
    ${CLANG_TIDY} "${FILE}" -- -std=c++17 2>&1 || true

    echo ""
    echo "── clang-format: ${FILE} ──"
    if ${CLANG_FORMAT} --dry-run --Werror "${FILE}" 2>/dev/null; then
        echo "  OK"
    else
        echo "  [!] ${FILE} needs formatting — run: clang-format -i ${FILE}"
        HAS_WARNINGS=1
    fi
done

echo ""
if [ ${HAS_WARNINGS} -eq 1 ]; then
    echo "⚠  Some files need formatting. Commit proceeds anyway (lax mode)."
fi
echo "✓  Pre-commit lint complete."

exit 0
