#!/usr/bin/env bash
set -euo pipefail

# Usage: ./buildhdocs.sh [OS_NAME]
# If OS_NAME is not provided, detect from uname.

if [[ ${1-} != "" ]]; then
  OS_NAME="$1"
else
  UNAME_S="$(uname -s)"
  case "$UNAME_S" in
    Darwin) OS_NAME="OSX" ;;
    Linux) OS_NAME="Linux" ;;
    CYGWIN*|MINGW*|MSYS*) OS_NAME="Windows" ;;
    *) OS_NAME="Unknown" ;;
  esac
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_ROOT="$ROOT_DIR/build/$OS_NAME"
BUILD_DIR="$BUILD_ROOT/Release"

mkdir -p "$BUILD_DIR"

cd "$BUILD_DIR"
cmake .. -G "Ninja" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Ensure hdoc sees a stable path regardless of OS build dir.
mkdir -p "$ROOT_DIR/build"
cp "$BUILD_DIR/compile_commands.json" "$ROOT_DIR/build/compile_commands.json"

echo "compile_commands.json: $ROOT_DIR/build/compile_commands.json"

# Generate documentation with hdoc (expects .hdoc.toml in ROOT_DIR).
cd "$ROOT_DIR"
hdoc
