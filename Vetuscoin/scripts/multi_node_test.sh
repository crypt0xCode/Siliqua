#!/usr/bin/env bash
# Three-node network scenario, run from separate terminal-like processes on one machine:
#   A mines its own coinbase, A pays B, B mines that payment into a block, C syncs it from B.
# Ends with a consensus check: do B and C agree on the resulting chain, byte for byte?
#
# Usage: ./scripts/multi_node_test.sh [workdir]
# Run from the project root (needs bin/Vetuscoin already built).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN="$PROJECT_ROOT/bin/Vetuscoin"
WORKDIR="${1:-/tmp/vetuscoin_network_test}"

if [ ! -x "$BIN" ]; then
    echo "Build the project first: $BIN not found." >&2
    exit 1
fi

rm -rf "$WORKDIR"
mkdir -p "$WORKDIR"

echo "=== Seeding a shared genesis chain for nodes A, B, C ==="
"$BIN" --seed "$WORKDIR/seed.dat"
cp "$WORKDIR/seed.dat" "$WORKDIR/a.dat"
cp "$WORKDIR/seed.dat" "$WORKDIR/b.dat"
cp "$WORKDIR/seed.dat" "$WORKDIR/c.dat"

echo
echo "=== Round 1: node A mines block #2 (its own coinbase), node B fetches and validates it ==="
"$BIN" --listen 19101 "$WORKDIR/a.dat" &
PID=$!
sleep 1
"$BIN" --connect 127.0.0.1 19101 "$WORKDIR/b.dat"
wait "$PID"
cp "$WORKDIR/a.dat" "$WORKDIR/c.dat" # C wasn't a party to round 1, hand it the same result out of band

# --address's own debug logging shares stdout with its output line, so pull out just the
# 40-hex-char address line rather than trusting the whole captured output to be clean.
ADDR_B=$("$BIN" --address "$WORKDIR/b.dat" 2>&1 | grep -E '^[0-9a-f]{40}$')
echo "Node B address: $ADDR_B"

echo
echo "=== Round 2: node A signs a payment to node B, node B queues it in its mempool ==="
"$BIN" --receive-tx 19102 "$WORKDIR/b.dat" &
PID=$!
sleep 1
"$BIN" --send 127.0.0.1 19102 "$WORKDIR/a.dat" "$ADDR_B" 1000000000
wait "$PID"

echo
echo "=== Round 3: node B mines the queued payment into block #3, node C fetches and validates it ==="
"$BIN" --listen 19103 "$WORKDIR/b.dat" &
PID=$!
sleep 1
"$BIN" --connect 127.0.0.1 19103 "$WORKDIR/c.dat"
wait "$PID"

echo
echo "=== Consensus check: do B and C agree on the chain? ==="
if diff -q "$WORKDIR/b.dat" "$WORKDIR/c.dat" >/dev/null; then
    echo "OK: node B and node C chains are byte-identical."
else
    echo "MISMATCH: node B and node C chains differ!" >&2
    exit 1
fi
