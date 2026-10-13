#!/usr/bin/env bash
# Hub-and-follower scenario with two persistent daemons on one machine:
#   HUB mines (--daemon <port> <chain>), FOLLOWER never mines (--daemon <port> <chain> hub --no-mine).
#   HUB pays FOLLOWER, FOLLOWER pays HUB back - both through the hub's mempool, with no
#   --listen / --connect / --receive-tx rounds. Ends with exact balance checks.
#
# Why --no-mine: two mining daemons fork away from each other, and a payment that spends coins
# from the losing fork is rejected by the other node.
#
# Usage: ./scripts/daemon_hub_test.sh [workdir]
# Run from the project root (needs bin/Siliqua already built). Takes about a minute.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN="$PROJECT_ROOT/bin/Siliqua"
WORKDIR="${1:-/tmp/siliqua_daemon_hub_test}"
HUB_PORT=19200
FOLLOWER_PORT=19201
REWARD=5000000000
FEE=1000
PAY_TO_FOLLOWER=1000000000
PAY_TO_HUB=300000000

if [ ! -x "$BIN" ]; then
    echo "Build the project first: $BIN not found." >&2
    exit 1
fi

rm -rf "$WORKDIR"
mkdir -p "$WORKDIR/hub" "$WORKDIR/follower"

PIDS=()
cleanup() {
    for pid in "${PIDS[@]:-}"; do
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    done
}
trap cleanup EXIT

# Prints the spendable total (satoshi) of a chain's wallet, or nothing if the chain file was
# being rewritten at that moment (the caller retries).
balance_of() {
    "$BIN" --balance "$1" 2>&1 | grep -oE '[0-9]+ satoshi total' | grep -oE '^[0-9]+' || true
}

wait_for_balance() { # <chain> <expected satoshi> <what>
    for _ in $(seq 1 40); do
        if [ "$(balance_of "$1")" = "$2" ]; then
            echo "OK: $3 = $2 satoshi"
            return 0
        fi
        sleep 2
    done
    echo "TIMEOUT: $3 never reached $2 (last seen: '$(balance_of "$1")')" >&2
    exit 1
}

echo "=== Seeding one shared genesis for the hub and the follower ==="
"$BIN" --seed "$WORKDIR/hub/chain.dat" >/dev/null 2>&1
cp "$WORKDIR/hub/chain.dat" "$WORKDIR/follower/chain.dat"
HUB_ADDR=$("$BIN" --address "$WORKDIR/hub/chain.dat" 2>&1 | grep -E '^[1-9A-HJ-NP-Za-km-z]{20,}$')
FOLLOWER_ADDR=$("$BIN" --address "$WORKDIR/follower/chain.dat" 2>&1 | grep -E '^[1-9A-HJ-NP-Za-km-z]{20,}$')
echo "Hub address:      $HUB_ADDR"
echo "Follower address: $FOLLOWER_ADDR"

echo
echo "=== Starting the hub (mines every few seconds) and the follower (--no-mine) ==="
"$BIN" --daemon "$HUB_PORT" "$WORKDIR/hub/chain.dat" >"$WORKDIR/hub/daemon.log" 2>&1 &
PIDS+=($!)
sleep 1
"$BIN" --daemon "$FOLLOWER_PORT" "$WORKDIR/follower/chain.dat" "127.0.0.1:$HUB_PORT" --no-mine \
    >"$WORKDIR/follower/daemon.log" 2>&1 &
PIDS+=($!)

echo
echo "=== Hub pays the follower $PAY_TO_FOLLOWER (waits until the hub has mined some coins) ==="
for _ in $(seq 1 30); do
    [ -n "$(balance_of "$WORKDIR/hub/chain.dat")" ] && [ "$(balance_of "$WORKDIR/hub/chain.dat")" -ge "$((PAY_TO_FOLLOWER + FEE))" ] && break
    sleep 2
done
"$BIN" --send 127.0.0.1 "$HUB_PORT" "$WORKDIR/hub/chain.dat" "$FOLLOWER_ADDR" "$PAY_TO_FOLLOWER" >/dev/null 2>&1
wait_for_balance "$WORKDIR/follower/chain.dat" "$PAY_TO_FOLLOWER" "follower balance after the hub's payment"

echo
echo "=== Follower pays the hub $PAY_TO_HUB (sent to the hub's port, follower daemon still running) ==="
"$BIN" --send 127.0.0.1 "$HUB_PORT" "$WORKDIR/follower/chain.dat" "$HUB_ADDR" "$PAY_TO_HUB" >/dev/null 2>&1
EXPECTED_FOLLOWER=$((PAY_TO_FOLLOWER - PAY_TO_HUB - FEE))
wait_for_balance "$WORKDIR/follower/chain.dat" "$EXPECTED_FOLLOWER" "follower balance after paying back"

echo
echo "=== Stopping both daemons and checking the hub's books ==="
cleanup
PIDS=()
sleep 1
HUB_OUT=$("$BIN" --balance "$WORKDIR/hub/chain.dat" 2>&1)
BLOCKS=$(echo "$HUB_OUT" | grep -oE 'Loaded [0-9]+ block' | grep -oE '[0-9]+')
HUB_BALANCE=$(echo "$HUB_OUT" | grep -oE '[0-9]+ satoshi total' | grep -oE '^[0-9]+')
# Every block but the genesis was mined by the hub. The fees it pays out come back to it as the
# miner, so only the two payment amounts change its total.
EXPECTED_HUB=$(( (BLOCKS - 1) * REWARD - PAY_TO_FOLLOWER + PAY_TO_HUB + FEE ))
if [ "$HUB_BALANCE" = "$EXPECTED_HUB" ]; then
    echo "OK: hub balance = $HUB_BALANCE ($((BLOCKS - 1)) mined block(s))"
else
    echo "MISMATCH: hub balance $HUB_BALANCE, expected $EXPECTED_HUB" >&2
    exit 1
fi
FOLLOWER_MINED=$(grep -c "daemon(mine)" "$WORKDIR/follower/daemon.log" || true)
if [ "$FOLLOWER_MINED" = "0" ]; then
    echo "OK: the follower mined no blocks"
else
    echo "MISMATCH: the follower mined $FOLLOWER_MINED block(s) despite --no-mine" >&2
    exit 1
fi
echo "All daemon-hub checks passed."
