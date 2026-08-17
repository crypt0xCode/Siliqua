#!/usr/bin/env bash
# Fork resolution test: two nodes independently mine competing blocks on top of the same
# genesis (a real fork, not a bug), and a third node must pick the chain with more
# cumulative work when it meets it - and must NOT switch away from a heavier chain onto a
# lighter one it's shown afterward.
#
# Usage: ./scripts/fork_test.sh [workdir]
# Run from the project root (needs bin/Vetuscoin already built).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN="$PROJECT_ROOT/bin/Vetuscoin"
WORKDIR="${1:-/tmp/vetuscoin_fork_test}"
PORT=19200

if [ ! -x "$BIN" ]; then
    echo "Build the project first: $BIN not found." >&2
    exit 1
fi

rm -rf "$WORKDIR"
mkdir -p "$WORKDIR"

# Mines one more block onto $1's chain by pairing it with a disposable connector peer.
extend() {
    local chain="$1"
    cp "$WORKDIR/seed.dat" "$WORKDIR/throwaway.dat" # run_connector needs an existing chain to validate against
    PORT=$((PORT + 1))
    "$BIN" --listen "$PORT" "$chain" &
    local pid=$!
    sleep 1
    "$BIN" --connect 127.0.0.1 "$PORT" "$WORKDIR/throwaway.dat" >/dev/null
    wait "$pid"
    rm -f "$WORKDIR/throwaway.dat" "$WORKDIR/throwaway.dat.wallet"
}

# Announces $1's current chain to $2, letting $2's run_connector decide whether to accept/reorg.
sync_to() {
    local from_chain="$1"
    local to_chain="$2"
    PORT=$((PORT + 1))
    "$BIN" --listen "$PORT" "$from_chain" &
    local pid=$!
    sleep 1
    "$BIN" --connect 127.0.0.1 "$PORT" "$to_chain"
    wait "$pid"
}

echo "=== Seeding a shared genesis chain for nodes A, D and E ==="
"$BIN" --seed "$WORKDIR/seed.dat"
cp "$WORKDIR/seed.dat" "$WORKDIR/a.dat"
cp "$WORKDIR/seed.dat" "$WORKDIR/d.dat"
cp "$WORKDIR/seed.dat" "$WORKDIR/e.dat"

echo
echo "=== A mines its own block #2 (the light branch: height 2 total) ==="
extend "$WORKDIR/a.dat"

echo
echo "=== D independently mines two DIFFERENT blocks on top of the same genesis ==="
echo "    (the heavy branch: height 3 total, and it does not share A's block #2 at all)"
extend "$WORKDIR/d.dat"
extend "$WORKDIR/d.dat"

echo
echo "=== E first syncs to A's (lighter) branch ==="
sync_to "$WORKDIR/a.dat" "$WORKDIR/e.dat"

echo
echo "=== E now meets D's heavier, competing branch - it should reorg onto it ==="
sync_to "$WORKDIR/d.dat" "$WORKDIR/e.dat"

if diff -q "$WORKDIR/d.dat" "$WORKDIR/e.dat" >/dev/null; then
    echo "OK: E reorged onto D's heavier chain."
else
    echo "FAIL: E did not adopt the heavier chain." >&2
    exit 1
fi

echo
echo "=== E is now shown A's lighter branch again - it must NOT switch back ==="
sync_to "$WORKDIR/a.dat" "$WORKDIR/e.dat"

if diff -q "$WORKDIR/d.dat" "$WORKDIR/e.dat" >/dev/null; then
    echo "OK: E correctly rejected the lighter chain and kept the heavier one."
else
    echo "FAIL: E switched to a lighter chain - fork rule is broken." >&2
    exit 1
fi
