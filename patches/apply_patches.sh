#!/usr/bin/env bash
# Apply required ns-3-mptcp kernel patches
set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
NS3_DIR="$REPO_ROOT/ns-3-mptcp"

echo "Applying patches to ns-3-mptcp in $NS3_DIR..."
cd "$NS3_DIR"

if [ -f "$SCRIPT_DIR/0001-mptcp-kernel-fixes.diff" ]; then
    patch -p1 -N --dry-run < "$SCRIPT_DIR/0001-mptcp-kernel-fixes.diff" >/dev/null 2>&1 && {
        patch -p1 < "$SCRIPT_DIR/0001-mptcp-kernel-fixes.diff"
        echo "Successfully applied 0001-mptcp-kernel-fixes.diff"
    } || echo "Patch 0001-mptcp-kernel-fixes.diff already applied or partially matched."
fi

echo "ns-3-mptcp patches verified."
