#!/usr/bin/env bash
# One-shot static scene using netcat.
#
# Usage:  ./examples/hello_world.sh
#
# A named pipe keeps the nc connection alive so the scene can be redrawn
# every frame (avoiding double-buffer flicker).  Press Ctrl+C to clear
# to black and exit.
#
# Requires: nc (netcat), a running raylib_server on localhost:7878.

HOST=localhost
PORT=7878

FIFO=$(mktemp -u)
mkfifo "$FIFO"

cleanup() {
  # Send one final clear, then close the FIFO so nc exits cleanly.
  printf '{"cmd":"ClearBackground","args":{"color":"BLACK"}}\n' >&3 2>/dev/null
  sleep 0.05
  exec 3>&-
  wait "$NC_PID" 2>/dev/null
  rm -f "$FIFO"
}
trap cleanup EXIT

# nc reads from the FIFO; runs in the background.
nc "$HOST" "$PORT" <"$FIFO" &
NC_PID=$!

# Open the FIFO for writing; keeps it open until we close fd 3.
exec 3>"$FIFO"

echo "Scene running — press Ctrl+C to clear and exit."

while kill -0 "$NC_PID" 2>/dev/null; do
  printf '%s\n' \
    '{"cmd":"ClearBackground","args":{"color":"RAYWHITE"}}' \
    '{"cmd":"DrawCircle","args":{"centerX":400,"centerY":280,"radius":100,"color":"RED"}}' \
    '{"cmd":"DrawText","args":{"text":"Hello!","posX":360,"posY":260,"fontSize":30,"color":"WHITE"}}' \
    '{"cmd":"DrawRectangle","args":{"posX":20,"posY":20,"width":180,"height":50,"color":"BLUE"}}' \
    '{"cmd":"DrawText","args":{"text":"raylib_server","posX":28,"posY":33,"fontSize":20,"color":"WHITE"}}' \
    '{"cmd":"DrawFPS","args":{"posX":10,"posY":570}}' \
    >&3
  sleep 0.016
done
