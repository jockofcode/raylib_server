#!/usr/bin/env bash
# Run every integration example against a live raylib_server and verify
# that expected log patterns appear and no warnings or errors occur.
#
# Usage:
#   bash scripts/test_examples.sh

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SERVER_BIN="$PROJECT_DIR/build/raylib_server"
EXAMPLES_DIR="$PROJECT_DIR/examples"
LOG_FILE="$(mktemp)"

SERVER_PID=""
PASS=0
FAIL=0

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BOLD='\033[1m'
NC='\033[0m'

cleanup() {
    if [[ -n "$SERVER_PID" ]]; then
        kill "$SERVER_PID" 2>/dev/null || true
    fi
    rm -f "$LOG_FILE"
}
trap cleanup EXIT

pass() { echo -e "  ${GREEN}PASS${NC}  $1"; PASS=$((PASS + 1)); }
fail() {
    echo -e "  ${RED}FAIL${NC}  $1"
    echo -e "        ${YELLOW}$2${NC}"
    FAIL=$((FAIL + 1))
}

# Kill a process and all its direct children with SIGKILL.
# We use SIGKILL (not SIGINT/SIGTERM) to avoid Ruby/bash ensure blocks that
# may call socket.write and block indefinitely.  The kernel closes all FDs
# on SIGKILL, so the server detects the disconnect via EOF on recv().
kill_tree() {
    local pid="$1"
    # Kill children before the parent so they don't get orphaned.
    pgrep -P "$pid" 2>/dev/null | while read -r cpid; do
        kill -KILL "$cpid" 2>/dev/null || true
    done
    kill -KILL "$pid" 2>/dev/null || true
}

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

echo -e "${BOLD}==> Building...${NC}"
make -C "$PROJECT_DIR" all -j4 2>&1 | grep -E "Error|error:|Built target|Linking|100%" || true
echo ""

if [[ ! -x "$SERVER_BIN" ]]; then
    echo "ERROR: server binary not found: $SERVER_BIN"
    exit 1
fi

# ---------------------------------------------------------------------------
# Start server
# ---------------------------------------------------------------------------

echo -e "${BOLD}==> Starting raylib_server...${NC}"
"$SERVER_BIN" --log-level info 2>"$LOG_FILE" &
SERVER_PID=$!
sleep 2

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "ERROR: server failed to start. Log:"
    cat "$LOG_FILE"
    exit 1
fi
echo "   PID $SERVER_PID  log -> $LOG_FILE"

# ---------------------------------------------------------------------------
# run_example NAME CMD DURATION PATTERNS...
#
# Runs CMD for DURATION seconds, then verifies:
#   - each PATTERN (ERE) appears in the server log lines emitted during the run
#   - no [WARNING] or [ERROR  ] lines appear
#   - the server is still alive afterward
# ---------------------------------------------------------------------------

run_example() {
    local name="$1"
    local cmd="$2"
    local duration="${3:-4}"
    shift 3
    local patterns=("$@")

    echo ""
    echo "  --- $name ---"

    # Snapshot log size before starting so we can isolate new entries.
    local before
    before=$(wc -c < "$LOG_FILE" | tr -d ' \n')

    eval "$cmd" >/dev/null 2>&1 &
    local pid=$!
    # Silence the shell's "Killed: 9" job notification.
    disown "$pid" 2>/dev/null || true
    sleep "$duration"

    # SIGKILL the process tree — avoids any blocking cleanup code.
    # The kernel closes all FDs on SIGKILL, so the server sees EOF on recv()
    # and logs the disconnect.
    kill_tree "$pid"

    # Allow time for server to detect EOF and flush the disconnect log entry.
    sleep 1

    # Extract only the log lines written during this run.
    local new_logs
    new_logs=$(tail -c "+$((before + 1))" "$LOG_FILE")

    local ok=true

    # Check every required pattern.
    for pattern in "${patterns[@]}"; do
        if ! echo "$new_logs" | grep -qE "$pattern"; then
            fail "$name" "missing log pattern: $pattern"
            ok=false
        fi
    done

    # Any WARNING or ERROR lines are a test failure.
    local bad
    bad=$(echo "$new_logs" | grep -E '\[WARNING\]|\[ERROR  \]' || true)
    if [[ -n "$bad" ]]; then
        local snippet
        snippet=$(echo "$bad" | head -3 | sed 's/^/          /')
        fail "$name" "unexpected WARNING/ERROR in logs:"$'\n'"$snippet"
        ok=false
    fi

    # Server must still be alive.
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        fail "$name" "server crashed during this example"
        ok=false
    fi

    if $ok; then pass "$name"; fi
}

# ---------------------------------------------------------------------------
# Run all examples
# ---------------------------------------------------------------------------

echo ""
echo -e "${BOLD}==> Running examples (~4 s each)...${NC}"

CONNECT_PAT="\[INFO[[:space:]]*\] client [0-9]+ connected"
DISCONNECT_PAT="\[INFO[[:space:]]*\] client [0-9]+ disconnected"

run_example "hello_world.sh"         "bash '$EXAMPLES_DIR/hello_world.sh'"             4  "$CONNECT_PAT" "$DISCONNECT_PAT"
run_example "bouncing_ball.rb"       "ruby '$EXAMPLES_DIR/bouncing_ball.rb'"           4  "$CONNECT_PAT" "$DISCONNECT_PAT"
run_example "color_showcase.rb"      "ruby '$EXAMPLES_DIR/color_showcase.rb'"          4  "$CONNECT_PAT" "$DISCONNECT_PAT"
run_example "shapes_demo.rb"         "ruby '$EXAMPLES_DIR/shapes_demo.rb'"             4  "$CONNECT_PAT" "$DISCONNECT_PAT"
run_example "camera_zoom.rb"         "ruby '$EXAMPLES_DIR/camera_zoom.rb'"             4  "$CONNECT_PAT" "$DISCONNECT_PAT"
run_example "splines_demo.rb"        "ruby '$EXAMPLES_DIR/splines_demo.rb'"            4  "$CONNECT_PAT" "$DISCONNECT_PAT"
run_example "render_texture_demo.rb" "ruby '$EXAMPLES_DIR/render_texture_demo.rb'"     6  "$CONNECT_PAT" "$DISCONNECT_PAT"

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

echo ""
echo "========================================"
total=$((PASS + FAIL))
if [[ $FAIL -eq 0 ]]; then
    echo -e "${GREEN}${BOLD}All $total examples passed.${NC}"
else
    echo -e "${RED}${BOLD}$FAIL/$total failed.${NC}  ($PASS passed)"
fi
echo "========================================"

[[ $FAIL -eq 0 ]]
