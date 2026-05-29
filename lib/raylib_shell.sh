#!/usr/bin/env bash
# raylib_shell.sh — bash helper functions for raylib_server.
#
# Uses bash's built-in /dev/tcp for zero-dependency TCP.
#
# Usage:
#   source lib/raylib_shell.sh
#   rls_connect              # connect to localhost:7878 (default)
#   rls_connect 7879         # custom port
#   rls_cmd  DrawCircle '{"centerX":400,"centerY":300,"radius":50,"color":"RED"}'
#   rls_sync GetScreenWidth  # blocks, prints JSON result
#   rls_sync GetScreenWidth '{}' | jq .width
#   rls_disconnect
#
# All functions write to the global file descriptor RLS_FD (default 42).
# Set RLS_FD before sourcing if 42 conflicts with another fd in your script.

RLS_FD=${RLS_FD:-42}
_RLS_HOST=localhost
_RLS_PORT=7878
_RLS_SEQ=0

# ---------------------------------------------------------------------------
# rls_connect [PORT [HOST]]
#   Open a TCP connection to raylib_server.
# ---------------------------------------------------------------------------
rls_connect() {
    local port=${1:-7878}
    local host=${2:-localhost}
    _RLS_HOST=$host
    _RLS_PORT=$port
    eval "exec ${RLS_FD}<>/dev/tcp/${host}/${port}" 2>/dev/null || {
        echo "rls_connect: failed to connect to ${host}:${port}" >&2
        return 1
    }
}

# ---------------------------------------------------------------------------
# rls_disconnect
#   Close the TCP connection.
# ---------------------------------------------------------------------------
rls_disconnect() {
    eval "exec ${RLS_FD}>&-" 2>/dev/null
    eval "exec ${RLS_FD}<&-" 2>/dev/null
}

# ---------------------------------------------------------------------------
# rls_cmd CMD [ARGS_JSON]
#   Send a fire-and-forget command (no id, no response expected).
#
#   rls_cmd ClearBackground '{"color":"RAYWHITE"}'
#   rls_cmd BeginDrawing
# ---------------------------------------------------------------------------
rls_cmd() {
    local cmd="$1"
    local args="${2-}"
    local msg
    if [[ -n "$args" && "$args" != 'null' && "$args" != '{}' ]]; then
        msg="{\"cmd\":\"${cmd}\",\"args\":${args}}"
    else
        msg="{\"cmd\":\"${cmd}\"}"
    fi
    printf '%s\n' "$msg" >&${RLS_FD}
}

# ---------------------------------------------------------------------------
# rls_sync CMD [ARGS_JSON]
#   Send a synchronous command and print the server's JSON response to stdout.
#   Returns 0 on ok:true, 1 on ok:false.
#
#   rls_sync GetScreenWidth
#   WIDTH=$(rls_sync GetScreenWidth | _rls_jq_field width)
# ---------------------------------------------------------------------------
rls_sync() {
    local cmd="$1"
    local args="${2-}"
    _RLS_SEQ=$(( _RLS_SEQ + 1 ))
    local id="sh${_RLS_SEQ}"
    local msg
    if [[ -n "$args" && "$args" != 'null' ]]; then
        msg="{\"id\":\"${id}\",\"cmd\":\"${cmd}\",\"args\":${args}}"
    else
        msg="{\"id\":\"${id}\",\"cmd\":\"${cmd}\"}"
    fi
    printf '%s\n' "$msg" >&${RLS_FD}
    local response
    IFS= read -r response <&${RLS_FD}
    printf '%s\n' "$response"
    # Return non-zero if ok:false
    if [[ "$response" == *'"ok":false'* ]]; then
        return 1
    fi
    return 0
}

# ---------------------------------------------------------------------------
# rls_draw_text TEXT POS_X POS_Y FONT_SIZE COLOR
#   Convenience wrapper for DrawText.
# ---------------------------------------------------------------------------
rls_draw_text() {
    local text="$1" pos_x="$2" pos_y="$3" font_size="$4" color="${5:-WHITE}"
    rls_cmd DrawText "{\"text\":\"${text}\",\"posX\":${pos_x},\"posY\":${pos_y},\"fontSize\":${font_size},\"color\":\"${color}\"}"
}

# ---------------------------------------------------------------------------
# rls_clear COLOR
#   Clear the background to a named color.
# ---------------------------------------------------------------------------
rls_clear() {
    rls_cmd ClearBackground "{\"color\":\"${1:-BLACK}\"}"
}

# ---------------------------------------------------------------------------
# rls_draw_fps [POS_X [POS_Y]]
# ---------------------------------------------------------------------------
rls_draw_fps() {
    rls_cmd DrawFPS "{\"posX\":${1:-10},\"posY\":${2:-10}}"
}

# ---------------------------------------------------------------------------
# rls_set_window TITLE WIDTH HEIGHT
# ---------------------------------------------------------------------------
rls_set_window() {
    local title="$1" w="${2:-800}" h="${3:-600}"
    rls_cmd SetWindowTitle "{\"title\":\"${title}\"}"
    rls_cmd SetWindowSize  "{\"width\":${w},\"height\":${h}}"
}

# ---------------------------------------------------------------------------
# rls_display_list_begin NAME
# rls_display_list_end
#   Wrap a block of draw commands in a display list.
#
#   rls_display_list_begin main
#   rls_clear RAYWHITE
#   rls_cmd DrawCircleV '{"center":[400,300],"radius":50,"color":"RED"}'
#   rls_display_list_end
# ---------------------------------------------------------------------------
rls_display_list_begin() {
    rls_cmd DisplayListBegin "{\"name\":\"${1}\"}"
}

rls_display_list_end() {
    rls_cmd DisplayListEnd
}

rls_display_list_delete() {
    rls_cmd DisplayListDelete "{\"name\":\"${1}\"}"
}

# ---------------------------------------------------------------------------
# _rls_jq_field FIELD
#   Minimal field extractor when jq is unavailable.
#   Reads JSON from stdin, prints the value of "FIELD".
#   Only handles simple string/number values.
# ---------------------------------------------------------------------------
_rls_jq_field() {
    local field="$1"
    sed -n "s/.*\"${field}\":\([^,}]*\).*/\1/p" | tr -d '"'
}

# ---------------------------------------------------------------------------
# rls_get_screen_width
# rls_get_screen_height
#   Print just the integer value.
# ---------------------------------------------------------------------------
rls_get_screen_width() {
    rls_sync GetScreenWidth | _rls_jq_field width
}

rls_get_screen_height() {
    rls_sync GetScreenHeight | _rls_jq_field height
}

# ---------------------------------------------------------------------------
# rls_get_mouse_position
#   Print "X Y" on one line.
# ---------------------------------------------------------------------------
rls_get_mouse_position() {
    local resp
    resp=$(rls_sync GetMousePosition)
    local x y
    x=$(printf '%s' "$resp" | _rls_jq_field '"x"')
    y=$(printf '%s' "$resp" | _rls_jq_field '"y"')
    printf '%s %s\n' "$x" "$y"
}
